/*
 * PROJECT:     LiberNT host tools
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     armasm64 emission + ARM64 SEH unwind synthesis for gas2armasm
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

static char *subst_symbols(const char *in);
static char *rewrite_local_labels(const char *in);
static char *strip_lo12(const char *in);
static char *trim(char *s);
static int is_ident_ch(char c);
static int eval_str(const char *s, int64_t *out);

static void emit_data(int sz, const char *args);
static void emit_ascii(const char *args, int zero);
static void translate_stmt(const char *in);
static void emit_equ(const char *name, const char *rhs);

static Str g_out;
static Str g_text;
static Str g_unwind;
static int g_text_align = 4;

static char g_cur_area[256] = "";

typedef struct { unsigned char b[4]; int len; } UCode;
typedef struct {
    UCode codes[256]; int ncodes;
    char funcname[256];
    char xdatalabel[300];
    char endlabel[300];
    char handler[256]; int has_handler;

    struct {
        char startlabel[300];
        UCode codes[64]; int ncodes;
    } epi[8];
    int nepi;
    int in_epi;
    int active;
    int seq;
} SehState;
static SehState g_seh;
static int g_seh_seq = 0;

static void out_area_text(void);

static void emit_init(int textalign)
{
    str_init(&g_out);
    str_init(&g_text);
    str_init(&g_unwind);
    g_text_align = textalign<2?2:textalign;
    g_seh.active=0; g_seh.nepi=0; g_seh.in_epi=0;
    g_cur_area[0]=0;
}

static void set_area(const char *decl)
{
    if(strcmp(g_cur_area,decl)!=0){
        strncpy(g_cur_area,decl,sizeof(g_cur_area)-1);
        str_add(&g_text,decl);
        str_addc(&g_text,'\n');
    }
}
static void out_area_text(void)
{
    char d[256];
    snprintf(d,sizeof(d),"    AREA |.text|,CODE,READONLY,ALIGN=%d", g_text_align);
    set_area(d);
}

static void ensure_area(void){ if(!g_cur_area[0]) out_area_text(); }

static void body(const char *s){ str_add(&g_text,s); str_addc(&g_text,'\n'); }

static void note_defined(const char *n){ name_add(&g_defined,n); }
static void note_ref(const char *n){ name_add(&g_referenced,n); }

static int looks_like_symbol(const char *n)
{
    if(!n[0]) return 0;
    if(isdigit((unsigned char)n[0])) return 0;
    if(n[0]=='.') return 0;
    if(!strncmp(n,"rL_",3)) return 0;
    return (isalpha((unsigned char)n[0])||n[0]=='_');
}

static void push_ucode(UCode *dst, int *n, const unsigned char *b, int len)
{
    UCode *u=&dst[(*n)++];
    for(int i=0;i<len;i++) u->b[i]=b[i];
    u->len=len;
}
static void add_code(const unsigned char *b, int len)
{
    if(g_seh.in_epi) push_ucode(g_seh.epi[g_seh.nepi-1].codes,&g_seh.epi[g_seh.nepi-1].ncodes,b,len);
    else push_ucode(g_seh.codes,&g_seh.ncodes,b,len);
}

static void uc_alloc(int64_t size)
{
    int64_t n=size/16;
    unsigned char b[4];
    if(n<=0x1f){ b[0]=(unsigned char)n; add_code(b,1); }
    else if(n<=0x7ff){ b[0]=(unsigned char)(0xc0|(n>>8)); b[1]=(unsigned char)(n&0xff); add_code(b,2); }
    else { b[0]=0xe0; b[1]=(unsigned char)((n>>16)&0xff); b[2]=(unsigned char)((n>>8)&0xff); b[3]=(unsigned char)(n&0xff); add_code(b,4); }
}
static void uc_save_fplr(int64_t off){ unsigned char b=0x40|(unsigned char)(off/8); add_code(&b,1); }
static void uc_save_fplr_x(int64_t off){ unsigned char b=0x80|(unsigned char)((off/8)-1); add_code(&b,1); }
static int regnum(const char *r){  while(*r && !isdigit((unsigned char)*r)) r++; return atoi(r); }
static void uc_save_regp(int reg,int64_t off){ int X=reg-19; unsigned char b[2]={(unsigned char)(0xc8|(X>>2)),(unsigned char)(((X&3)<<6)|(off/8))}; add_code(b,2); }
static void uc_save_regp_x(int reg,int64_t off){ int X=reg-19; unsigned char b[2]={(unsigned char)(0xcc|(X>>2)),(unsigned char)(((X&3)<<6)|((off/8)-1))}; add_code(b,2); }
static void uc_save_reg(int reg,int64_t off){ int X=reg-19; unsigned char b[2]={(unsigned char)(0xd0|(X>>2)),(unsigned char)(((X&3)<<6)|(off/8))}; add_code(b,2); }
static void uc_save_reg_x(int reg,int64_t off){ int X=reg-19; unsigned char b[2]={(unsigned char)(0xd4|(X>>3)),(unsigned char)(((X&7)<<5)|((off/8)-1))}; add_code(b,2); }
static void uc_save_fregp(int reg,int64_t off){ int X=reg-8; unsigned char b[2]={(unsigned char)(0xd8|(X>>2)),(unsigned char)(((X&3)<<6)|(off/8))}; add_code(b,2); }
static void uc_save_fregp_x(int reg,int64_t off){ int X=reg-8; unsigned char b[2]={(unsigned char)(0xda|(X>>2)),(unsigned char)(((X&3)<<6)|((off/8)-1))}; add_code(b,2); }
static void uc_set_fp(void){ unsigned char b=0xe1; add_code(&b,1); }
static void uc_add_fp(int64_t off){ if(off==0){ uc_set_fp(); return; } unsigned char b[2]={0xe2,(unsigned char)(off/8)}; add_code(b,2); }
static void uc_nop(void){ unsigned char b=0xe3; add_code(&b,1); }

static int64_t ev1(const char *s){ char *x=subst_symbols(s); int64_t v=0; eval_str(trim(x),&v); free(x); return v; }

static void parse_reg_off(const char *args, char *regbuf, int64_t *off)
{
    const char *p=args; while(*p==' '||*p=='\t')p++;
    int i=0; while(*p && *p!=',' && *p!=' ' && *p!='\t') regbuf[i++]=*p++;
    regbuf[i]=0;
    while(*p==' '||*p=='\t'||*p==',')p++;
    *off = *p ? ev1(p) : 0;
}

static void seh_directive(const char *name, const char *args)
{
    if(!strcmp(name,".seh_proc")){
        memset(&g_seh,0,sizeof(g_seh));
        g_seh.active=1;
        char n[256]; sscanf(args," %255s",n);

        strncpy(g_seh.funcname,n,sizeof(g_seh.funcname)-1);
        g_seh.seq = ++g_seh_seq;
        snprintf(g_seh.xdatalabel,sizeof(g_seh.xdatalabel),"|%s$xdata%d|",g_seh.funcname,g_seh.seq);
        snprintf(g_seh.endlabel,sizeof(g_seh.endlabel),"|%s$end%d|",g_seh.funcname,g_seh.seq);
        return;
    }
    if(!strcmp(name,".seh_endprologue")) return;
    if(!strcmp(name,".seh_stackalloc")){ uc_alloc(ev1(args)); return; }
    if(!strcmp(name,".seh_save_fplr")){ uc_save_fplr(ev1(args)); return; }
    if(!strcmp(name,".seh_save_fplr_x")){ uc_save_fplr_x(ev1(args)); return; }
    if(!strcmp(name,".seh_save_regp")){ char r[32]; int64_t o; parse_reg_off(args,r,&o); uc_save_regp(regnum(r),o); return; }
    if(!strcmp(name,".seh_save_regp_x")){ char r[32]; int64_t o; parse_reg_off(args,r,&o); uc_save_regp_x(regnum(r),o); return; }
    if(!strcmp(name,".seh_save_reg")){ char r[32]; int64_t o; parse_reg_off(args,r,&o); uc_save_reg(regnum(r),o); return; }
    if(!strcmp(name,".seh_save_reg_x")){ char r[32]; int64_t o; parse_reg_off(args,r,&o); uc_save_reg_x(regnum(r),o); return; }
    if(!strcmp(name,".seh_save_fregp")){ char r[32]; int64_t o; parse_reg_off(args,r,&o); uc_save_fregp(regnum(r),o); return; }
    if(!strcmp(name,".seh_save_fregp_x")){ char r[32]; int64_t o; parse_reg_off(args,r,&o); uc_save_fregp_x(regnum(r),o); return; }
    if(!strcmp(name,".seh_set_fp")){ uc_set_fp(); return; }
    if(!strcmp(name,".seh_add_fp")){ uc_add_fp(ev1(args)); return; }
    if(!strcmp(name,".seh_nop")){ uc_nop(); return; }
    if(!strcmp(name,".seh_handler")){
        char h[256]; sscanf(args," %255[^,]",h); char *ht=trim(h);
        strncpy(g_seh.handler,ht,sizeof(g_seh.handler)-1); g_seh.has_handler=1;
        note_ref(ht);
        return;
    }
    if(!strcmp(name,".seh_startepilogue")){
        g_seh.in_epi=1;
        int k=g_seh.nepi++;
        g_seh.epi[k].ncodes=0;
        snprintf(g_seh.epi[k].startlabel,sizeof(g_seh.epi[k].startlabel),"|%s$epi%d_%d|",g_seh.funcname,g_seh.seq,k);

        body(g_seh.epi[k].startlabel);
        return;
    }
    if(!strcmp(name,".seh_endepilogue")){ g_seh.in_epi=0; return; }
    if(!strcmp(name,".seh_context")){ unsigned char b=0xea; add_code(&b,1); return; }
    if(!strcmp(name,".seh_pushframe")){ unsigned char b=0xe9; add_code(&b,1); return; }
    if(!strcmp(name,".seh_save_next")){ unsigned char b=0xe6; add_code(&b,1); return; }

}

static void seh_endproc(void)
{

    body(g_seh.endlabel);

    unsigned char bytes[1200]; int nb=0;
    for(int i=g_seh.ncodes-1;i>=0;i--)
        for(int j=0;j<g_seh.codes[i].len;j++) bytes[nb++]=g_seh.codes[i].b[j];
    bytes[nb++]=0xe4;
    int prolog_bytes = nb;

    int epi_index[8]; int epi_nb[8];
    for(int e=0;e<g_seh.nepi;e++){
        epi_index[e]=nb;
        for(int i=0;i<g_seh.epi[e].ncodes;i++)
            for(int j=0;j<g_seh.epi[e].codes[i].len;j++) bytes[nb++]=g_seh.epi[e].codes[i].b[j];
        bytes[nb++]=0xe4;
        epi_nb[e]=nb-epi_index[e];
    }
    (void)prolog_bytes; (void)epi_nb;
    while(nb%4) bytes[nb++]=0xe3;
    int codeWords = nb/4;

    int xbit = g_seh.has_handler?1:0;
    long hi = ((long)codeWords<<27) | ((long)g_seh.nepi<<22) | ((long)xbit<<20);

    str_add(&g_unwind,"    AREA |.xdata|,DATA,READONLY,ALIGN=2\n");
    str_add(&g_unwind,g_seh.xdatalabel); str_addc(&g_unwind,'\n');
    {
        char line[512];
        snprintf(line,sizeof(line),"    DCD 0x%lx + ((%s - |%s|)/4)\n", hi, g_seh.endlabel, g_seh.funcname);
        str_add(&g_unwind,line);

        for(int e=0;e<g_seh.nepi;e++){

            snprintf(line,sizeof(line),"    DCD (%d*0x400000) + ((%s - |%s|)/4)\n",
                     epi_index[e], g_seh.epi[e].startlabel, g_seh.funcname);
            str_add(&g_unwind,line);
        }

        str_add(&g_unwind,"    DCB ");
        for(int i=0;i<nb;i++){ char t[8]; snprintf(t,sizeof(t),"%s0x%02x",i?",":"",bytes[i]); str_add(&g_unwind,t); }
        str_addc(&g_unwind,'\n');
        if(g_seh.has_handler){
            snprintf(line,sizeof(line),"    DCD |%s|\n    RELOC 2\n    DCD 0\n",g_seh.handler);
            str_add(&g_unwind,line);
        }
    }

    str_add(&g_unwind,"    AREA |.pdata|,DATA,READONLY,ALIGN=2\n");
    {
        char line[512];
        snprintf(line,sizeof(line),"    DCD |%s|\n    RELOC 2\n    DCD %s\n    RELOC 2\n",g_seh.funcname,g_seh.xdatalabel);
        str_add(&g_unwind,line);
    }
    g_seh.active=0;
}

static void collect_refs_from_operand(const char *op)
{

    const char *p=op; while(*p==' '||*p=='\t')p++;
    if(*p=='#') p++;
    char nm[256]; int i=0;
    if(isalpha((unsigned char)*p)||*p=='_'||*p=='.'){
        while(is_ident_ch(*p)&&i<255) nm[i++]=*p++;
        nm[i]=0;
        if(looks_like_symbol(nm)) note_ref(nm);
    }
}

static void emit_instruction(const char *stmt);

static void translate_stmt(const char *in)
{

    char work[4096];
    strncpy(work,in,sizeof(work)-1); work[sizeof(work)-1]=0;
    char *s=trim(work);

    for(;;){

        char *p=s; char nm[512]; int i=0;
        if(isdigit((unsigned char)*p)){
            while(isdigit((unsigned char)*p)) nm[i++]=*p++;
        } else if(isalpha((unsigned char)*p)||*p=='_'||*p=='.'||*p=='$'){
            while(is_ident_ch(*p)) nm[i++]=*p++;
        }
        if(i>0 && *p==':'){
            nm[i]=0; p++;

            if(isdigit((unsigned char)nm[0])){
                ensure_area();
                body(nm);
            } else if(nm[0]=='.'){
                char *m=rewrite_local_labels(nm);
                ensure_area();
                body(m); free(m);
            } else {
                ensure_area();
                char line[600]; snprintf(line,sizeof(line),"|%s|",nm);
                body(line);
                note_defined(nm);
            }
            s=trim(p);
            if(*s==0) return;
            continue;
        }
        break;
    }

    if(*s==0) return;

    if(*s=='.'){
        char d[64]; int i=0; const char *p=s; while(*p && !isspace((unsigned char)*p)&&i<63) d[i++]=*p++; d[i]=0;
        const char *args=p; while(*args==' '||*args=='\t')args++;

        if(!strcmp(d,".seh_endproc")){ if(g_seh.active) seh_endproc(); return; }
        if(!strncmp(d,".seh_",5)){ if(g_seh.active||!strcmp(d,".seh_proc")) seh_directive(d,args); return; }
        if(!strcmp(d,".text")){ out_area_text(); return; }
        if(!strcmp(d,".code")||!strcmp(d,".code64")||!strcmp(d,".code32")){ out_area_text(); return; }
        if(!strcmp(d,".data")){ set_area("    AREA |.data|,DATA,ALIGN=4"); return; }
        if(!strcmp(d,".bss")){ set_area("    AREA |.bss|,NOINIT,ALIGN=4"); return; }
        if(!strcmp(d,".section")){

            char sec[128]; int k=0; const char *q=args; while(*q&&*q!=','&&!isspace((unsigned char)*q)&&k<127) sec[k++]=*q++; sec[k]=0;
            char *nm = sec; if(nm[0]=='.') nm++;
            char decl[256];
            if(!strcmp(sec,".rdata")) snprintf(decl,sizeof(decl),"    AREA |.rdata|,DATA,READONLY,ALIGN=4");
            else if(!strcmp(sec,".drectve")) snprintf(decl,sizeof(decl),"    AREA |.drectve|,DRECTVE");
            else snprintf(decl,sizeof(decl),"    AREA |%s|,DATA,ALIGN=4",sec);
            set_area(decl);
            (void)nm;
            return;
        }
        if(!strcmp(d,".global")||!strcmp(d,".globl")){
            char n[256]; sscanf(args," %255s",n); char *nt=trim(n);
            char line[600]; snprintf(line,sizeof(line),"    EXPORT |%s|",nt); body(line);
            name_add(&g_globals,nt);
            return;
        }
        if(!strcmp(d,".extern")){ return; }
        if(!strcmp(d,".weakref")){ char a[256], t[256]; if(sscanf(args," %255[^, \t] , %255s",a,t)==2){ char line[600]; snprintf(line,sizeof(line),"    IMPORT |%s|, WEAK |%s|",trim(a),trim(t)); body(line); } return; }
        if(!strcmp(d,".weak")){ char n[256]; sscanf(args," %255s",n); char *nt=trim(n); char line[600]; snprintf(line,sizeof(line),"    EXPORT |%s| [WEAK]",nt); body(line); name_add(&g_globals,nt); return; }
        if(!strcmp(d,".align")||!strcmp(d,".p2align")){ int64_t e=ev1(args); char line[64]; snprintf(line,sizeof(line),"    ALIGN %lld",(long long)(1LL<<e)); body(line); return; }
        if(!strcmp(d,".balign")){ int64_t b=ev1(args); char line[64]; snprintf(line,sizeof(line),"    ALIGN %lld",(long long)b); body(line); return; }
        if(!strcmp(d,".space")||!strcmp(d,".skip")||!strcmp(d,".zero")){ int64_t n=ev1(args); char line[64]; snprintf(line,sizeof(line),"    SPACE %lld",(long long)n); body(line); return; }
        if(!strcmp(d,".byte")){ emit_data(1,args); return; }
        if(!strcmp(d,".hword")||!strcmp(d,".2byte")||!strcmp(d,".short")){ emit_data(2,args); return; }
        if(!strcmp(d,".word")||!strcmp(d,".4byte")||!strcmp(d,".long")){ emit_data(4,args); return; }
        if(!strcmp(d,".quad")||!strcmp(d,".8byte")||!strcmp(d,".xword")){ emit_data(8,args); return; }
        if(!strcmp(d,".ascii")){ emit_ascii(args,0); return; }
        if(!strcmp(d,".asciz")||!strcmp(d,".string")){ emit_ascii(args,1); return; }
        if(!strcmp(d,".ltorg")||!strcmp(d,".pool")){ body("    LTORG"); return; }

        if(!strcmp(d,".intel_syntax")||!strcmp(d,".att_syntax")||!strncmp(d,".cfi",4)||
           !strcmp(d,".altmacro")||!strcmp(d,".noaltmacro")||!strcmp(d,".func")||!strcmp(d,".endfunc")||
           !strcmp(d,".type")||!strcmp(d,".size")||!strcmp(d,".file")||!strcmp(d,".loc")||
           !strcmp(d,".arch")||!strcmp(d,".arch_extension")||!strcmp(d,".cpu")||!strcmp(d,".fpu")||
           !strcmp(d,".thumb")||!strcmp(d,".syntax")||!strcmp(d,".hidden")||
           !strcmp(d,".protected")||!strcmp(d,".internal")||!strcmp(d,".local")||!strcmp(d,".comm")||
           !strcmp(d,".ident")||!strcmp(d,".def")||!strcmp(d,".scl")||!strcmp(d,".endef")){
            return;
        }

        fprintf(stderr,"gas2armasm: %s: note: dropped directive '%s'\n", g_infile, d);
        return;
    }

    if(!strstr(g_cur_area,"CODE")) out_area_text();
    else ensure_area();
    emit_instruction(s);
}

static int count_data_syms(const char *s)
{
    int n=0; const char *p=s; int inq=0; char qc=0;
    while(*p){
        if(inq){ if(*p=='\\'&&p[1])p++; else if(*p==qc)inq=0; p++; continue; }
        if(*p=='"'||*p=='\''){ inq=1;qc=*p; p++; continue; }
        if(isalpha((unsigned char)*p)||*p=='_'||*p=='.'){
            char nm[256]; int i=0;
            while(is_ident_ch(*p)){ if(i<255)nm[i++]=*p; p++; }
            nm[i]=0;
            if(nm[0]=='.') continue;
            if(sym_find(nm)) continue;
            n++;
        } else p++;
    }
    return n;
}

static void emit_data(int sz, const char *args)
{
    const char *dc = sz==1?"DCB": sz==2?"DCW": sz==4?"DCD":"DCQ";
    char buf[8192]; strncpy(buf,args,sizeof(buf)-1); buf[sizeof(buf)-1]=0;
    char *p=buf;
    Str line; str_init(&line);
    str_add(&line,"    "); str_add(&line,dc); str_add(&line," ");
    int first=1, depth=0, inq=0; char q=0;
    char item[2048]; int ii=0;
    int done=0;
    while(!done){
        char c=*p;
        if(inq){ item[ii++]=c; if(c=='\\'&&p[1]){p++;item[ii++]=*p;} else if(c==q)inq=0; p++; }
        else if(c=='"'||c=='\''){ inq=1;q=c; item[ii++]=c; p++; }
        else if(c=='('){ depth++; item[ii++]=c; p++; }
        else if(c==')'){ depth--; item[ii++]=c; p++; }
        else if((c==','&&depth==0)||c==0){
            if(c==0) done=1;
            item[ii]=0; char *it=trim(item); ii=0;
            if(*it){
                char *sym=subst_symbols(it);
                char *m=rewrite_local_labels(sym);
                int64_t cv;
                int nsym=count_data_syms(m);
                if(eval_str(trim(m),&cv) || nsym!=1 || sz!=4){
                    if(nsym>=1) collect_refs_from_operand(m);
                    if(!first) str_add(&line,",");
                    str_add(&line,m); first=0;
                } else {
                    collect_refs_from_operand(m);
                    if(!first){ body(line.p); free(line.p); str_init(&line); str_add(&line,"    "); str_add(&line,dc); str_add(&line," "); first=1; }
                    char one[2100]; snprintf(one,sizeof(one),"    %s %s",dc,trim(m));
                    body(one);
                    body("    RELOC 1");
                }
                free(sym); free(m);
            }
            if(!done) p++;
        } else { item[ii++]=c; p++; }
    }
    if(!first) body(line.p);
    free(line.p);
}

static void emit_ascii(const char *args, int zero)
{

    Str out; str_init(&out); str_add(&out,"    DCB ");
    const char *p=args; int first=1;
    while(*p){
        while(*p==' '||*p=='\t'||*p==',')p++;
        if(!*p) break;
        if(*p=='"'){
            p++;
            while(*p && *p!='"'){
                unsigned char c;
                if(*p=='\\'){
                    p++;
                    switch(*p){
                        case 'n': c='\n'; break; case 'r': c='\r'; break; case 't': c='\t'; break;
                        case '0': c=0; break; case '\\': c='\\'; break; case '"': c='"'; break;
                        default: c=(unsigned char)*p; break;
                    }
                    p++;
                } else { c=(unsigned char)*p; p++; }
                char t[8]; snprintf(t,sizeof(t),"%s0x%02x",first?"":",",c); str_add(&out,t); first=0;
            }
            if(*p=='"') p++;
        } else {

            char item[256]; int i=0;
            while(*p && *p!=',' && i<255) item[i++]=*p++;
            item[i]=0; int64_t v=ev1(item);
            char t[16]; snprintf(t,sizeof(t),"%s0x%02x",first?"":",",(unsigned)(v&0xff)); str_add(&out,t); first=0;
        }
    }
    if(zero){ char t[8]; snprintf(t,sizeof(t),"%s0x00",first?"":","); str_add(&out,t); }
    body(out.p); free(out.p);
}

static char *fold_imm(const char *in)
{
    Str o; str_init(&o);
    const char *p=in; int inq=0; char q=0;
    while(*p){
        if(inq){ str_addc(&o,*p); if(*p=='\\'&&p[1]){p++;str_addc(&o,*p);} else if(*p==q) inq=0; p++; continue; }
        if(*p=='"'||*p=='\''){ inq=1;q=*p; str_addc(&o,*p); p++; continue; }
        if(p[0]=='#' && p[1]=='('){
            const char *s=p+1; int depth=0; const char *e=s;
            for(; *e; e++){ if(*e=='(')depth++; else if(*e==')'){ depth--; if(depth==0){ e++; break; } } }
            size_t n=(size_t)(e-s);
            char *expr=malloc(n+1); memcpy(expr,s,n); expr[n]=0;

            int64_t v;
            if(eval_str(expr,&v)){ char num[32]; snprintf(num,sizeof(num),"#%lld",(long long)v); str_add(&o,num); free(expr); p=e; continue; }
            free(expr);
            str_addc(&o,*p); p++; continue;
        }
        str_addc(&o,*p); p++;
    }
    return o.p;
}

static char *rewrite_numeric_refs(const char *in)
{
    Str o; str_init(&o);
    const char *p=in; int inq=0; char q=0;
    while(*p){
        if(inq){ str_addc(&o,*p); if(*p=='\\'&&p[1]){p++;str_addc(&o,*p);} else if(*p==q) inq=0; p++; continue; }
        if(*p=='"'||*p=='\''){ inq=1;q=*p; str_addc(&o,*p); p++; continue; }
        if(isdigit((unsigned char)*p) && (p==in || !is_ident_ch(p[-1]))){
            const char *d=p; while(isdigit((unsigned char)*d)) d++;
            if((*d=='f'||*d=='b') && !is_ident_ch(d[1])){
                char dir=*d;
                str_add(&o, dir=='f'?"%f":"%b");
                str_addn(&o,p,(size_t)(d-p));
                p=d+1;
                continue;
            }
        }
        str_addc(&o,*p); p++;
    }
    return o.p;
}

static char *fix_addsub_imm(const char *in)
{
    char mn[16]; int i=0; const char *p=in; while(*p==' '||*p=='\t')p++;
    while(*p && !isspace((unsigned char)*p) && i<15) mn[i++]=*p++; mn[i]=0;
    int is_as = (!strcmp(mn,"add")||!strcmp(mn,"adds")||!strcmp(mn,"sub")||!strcmp(mn,"subs")||
                 !strcmp(mn,"cmp")||!strcmp(mn,"cmn"));
    if(!is_as) return strdup(in);
    if(strstr(in,"lsl")||strstr(in,"LSL")) return strdup(in);
    Str o; str_init(&o);
    const char *q=in;
    while(*q){
        if(*q=='#' && (isdigit((unsigned char)q[1]))){
            const char *s=q+1; char *end; long long v=strtoll(s,&end,0);
            if(v>0xfff && (v & 0xfff)==0){
                char t[64]; snprintf(t,sizeof(t),"#%lld, lsl #12",v>>12); str_add(&o,t); q=end; continue;
            }
        }
        str_addc(&o,*q); q++;
    }
    return o.p;
}

static void emit_instruction(const char *stmt)
{
    char *sym = subst_symbols(stmt);
    char *fim = fold_imm(sym);
    char *as  = fix_addsub_imm(fim); free(fim); fim=as;
    char *num = rewrite_numeric_refs(fim);
    char *loc = rewrite_local_labels(num);
    char *lo  = strip_lo12(loc);

    {
        char mn[32]; int i=0; const char *p=lo; while(*p==' '||*p=='\t')p++;
        while(*p && !isspace((unsigned char)*p) && i<31) mn[i++]=*p++; mn[i]=0;
        const char *ops=p; while(*ops==' '||*ops=='\t')ops++;
        int isbranch = (!strcmp(mn,"b")||!strcmp(mn,"bl")||!strncmp(mn,"b.",2)||
                        !strcmp(mn,"cbz")||!strcmp(mn,"cbnz")||!strcmp(mn,"tbz")||!strcmp(mn,"tbnz")||
                        !strcmp(mn,"adr")||!strcmp(mn,"adrp")||!strcmp(mn,"adrl"));
        if(isbranch){

            const char *last=strrchr(ops,',');
            last = last? last+1 : ops;
            collect_refs_from_operand(last);
        }

        const char *eq=strchr(ops,'=');
        if(eq && (!strcmp(mn,"ldr")||!strcmp(mn,"ldrsw"))) collect_refs_from_operand(eq+1);
    }
    char line[4096]; snprintf(line,sizeof(line),"    %s",lo);
    body(line);
    free(sym); free(fim); free(num); free(loc); free(lo);
}

static void emit_equ(const char *name, const char *rhs)
{
    char *sym = subst_symbols(rhs);
    char *num = rewrite_numeric_refs(sym);
    char *loc = rewrite_local_labels(num);
    char *lo  = strip_lo12(loc);
    collect_refs_from_operand(lo);
    char line[4096]; snprintf(line,sizeof(line),"|%s| EQU %s",name,trim(lo));
    body(line);
    note_defined(name);
    free(sym); free(num); free(loc); free(lo);
}

static void emit_finish(void)
{

    str_add(&g_out,"; Generated by gas2armasm from ");
    str_add(&g_out,g_infile); str_addc(&g_out,'\n');
    for(NameNode *n=g_referenced;n;n=n->next){
        if(name_in(g_defined,n->name)) continue;
        if(sym_find(n->name)) continue;
        char line[600]; snprintf(line,sizeof(line),"    IMPORT |%s|\n",n->name); str_add(&g_out,line);
    }
    str_add(&g_out,g_text.p);
    if(g_unwind.len){ str_addc(&g_out,'\n'); str_add(&g_out,g_unwind.p); }
    str_add(&g_out,"\n    END\n");
}

static void emit_write(FILE *f){ fwrite(g_out.p,1,g_out.len,f); }
