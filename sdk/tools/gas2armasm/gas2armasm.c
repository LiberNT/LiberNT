/*
 * PROJECT:     LiberNT host tools
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Translate C-preprocessed GNU-as AArch64 assembly to armasm64 syntax
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

static const char *g_infile = "<stdin>";

static void fail(const char *msg)
{
    fprintf(stderr, "gas2armasm: %s: %s\n", g_infile, msg);
    exit(1);
}

typedef struct { char *p; size_t len, cap; } Str;
static void str_init(Str *s){ s->p=malloc(256); s->p[0]=0; s->len=0; s->cap=256; }
static void str_reserve(Str *s, size_t n){ if(s->len+n+1>s->cap){ while(s->len+n+1>s->cap) s->cap*=2; s->p=realloc(s->p,s->cap);} }
static void str_addn(Str *s, const char *t, size_t n){ str_reserve(s,n); memcpy(s->p+s->len,t,n); s->len+=n; s->p[s->len]=0; }
static void str_add(Str *s, const char *t){ str_addn(s,t,strlen(t)); }
static void str_addc(Str *s, char c){ str_reserve(s,1); s->p[s->len++]=c; s->p[s->len]=0; }

typedef struct { char **v; size_t n, cap; } Lines;
static void lines_init(Lines *L){ L->v=malloc(sizeof(char*)*64); L->n=0; L->cap=64; }
static void lines_push(Lines *L, char *s){ if(L->n==L->cap){ L->cap*=2; L->v=realloc(L->v,sizeof(char*)*L->cap);} L->v[L->n++]=s; }

typedef struct Sym { char *name; int64_t val; int defined; struct Sym *next; } Sym;
static Sym *g_syms = NULL;
static Sym *sym_find(const char *name){ for(Sym *s=g_syms;s;s=s->next) if(!strcmp(s->name,name)) return s; return NULL; }
static void sym_set(const char *name, int64_t v){
    Sym *s=sym_find(name);
    if(!s){ s=malloc(sizeof(Sym)); s->name=strdup(name); s->next=g_syms; g_syms=s; }
    s->val=v; s->defined=1;
}

typedef struct NameNode { char *name; struct NameNode *next; } NameNode;
static NameNode *g_defined=NULL, *g_referenced=NULL, *g_globals=NULL;
static int name_in(NameNode *l, const char *n){ for(;l;l=l->next) if(!strcmp(l->name,n)) return 1; return 0; }
static void name_add(NameNode **l, const char *n){ if(name_in(*l,n)) return; NameNode *x=malloc(sizeof(NameNode)); x->name=strdup(n); x->next=*l; *l=x; }

typedef struct { const char *s; int err; } EvalCtx;
static int64_t eval_expr(EvalCtx *c);

static void skipws(EvalCtx *c){ while(*c->s==' '||*c->s=='\t') c->s++; }

static int64_t eval_primary(EvalCtx *c)
{
    skipws(c);
    if(*c->s=='('){ c->s++; int64_t v=eval_expr(c); skipws(c); if(*c->s==')') c->s++; else c->err=1; return v; }
    if(*c->s=='-'){ c->s++; return -eval_primary(c); }
    if(*c->s=='+'){ c->s++; return eval_primary(c); }
    if(*c->s=='~'){ c->s++; return ~eval_primary(c); }
    if(*c->s=='!'){ c->s++; return !eval_primary(c); }
    if(isdigit((unsigned char)*c->s)){
        char *end; int64_t v;
        if(c->s[0]=='0' && (c->s[1]=='x'||c->s[1]=='X')) v=(int64_t)strtoull(c->s,&end,16);
        else if(c->s[0]=='0' && (c->s[1]=='b'||c->s[1]=='B')){ end=(char*)c->s+2; v=0; while(*end=='0'||*end=='1'){ v=v*2+(*end-'0'); end++; } }
        else v=(int64_t)strtoull(c->s,&end,10);
        c->s=end; return v;
    }
    if(isalpha((unsigned char)*c->s)||*c->s=='_'||*c->s=='.'||*c->s=='$'){
        char nm[256]; int i=0;
        while(isalnum((unsigned char)*c->s)||*c->s=='_'||*c->s=='.'||*c->s=='$'){ if(i<255) nm[i++]=*c->s; c->s++; }
        nm[i]=0;
        Sym *s=sym_find(nm);
        if(s) return s->val;
        c->err=1; return 0;
    }
    c->err=1; return 0;
}
static int64_t eval_mul(EvalCtx *c){
    int64_t v=eval_primary(c);
    for(;;){ skipws(c);
        if(*c->s=='*'){ c->s++; v*=eval_primary(c); }
        else if(*c->s=='/'){ c->s++; int64_t d=eval_primary(c); v = d? v/d : 0; }
        else if(*c->s=='%'){ c->s++; int64_t d=eval_primary(c); v = d? v%d : 0; }
        else break;
    }
    return v;
}
static int64_t eval_add(EvalCtx *c){
    int64_t v=eval_mul(c);
    for(;;){ skipws(c);
        if(*c->s=='+'){ c->s++; v+=eval_mul(c); }
        else if(*c->s=='-'){ c->s++; v-=eval_mul(c); }
        else break;
    }
    return v;
}
static int64_t eval_shift(EvalCtx *c){
    int64_t v=eval_add(c);
    for(;;){ skipws(c);
        if(c->s[0]=='<'&&c->s[1]=='<'){ c->s+=2; v<<=eval_add(c); }
        else if(c->s[0]=='>'&&c->s[1]=='>'){ c->s+=2; v>>=eval_add(c); }
        else break;
    }
    return v;
}
static int64_t eval_rel(EvalCtx *c){
    int64_t v=eval_shift(c);
    for(;;){ skipws(c);
        if(c->s[0]=='<'&&c->s[1]=='='){ c->s+=2; v = (v<=eval_shift(c)); }
        else if(c->s[0]=='>'&&c->s[1]=='='){ c->s+=2; v = (v>=eval_shift(c)); }
        else if(c->s[0]=='<'&&c->s[1]!='<'){ c->s++; v = (v<eval_shift(c)); }
        else if(c->s[0]=='>'&&c->s[1]!='>'){ c->s++; v = (v>eval_shift(c)); }
        else break;
    }
    return v;
}
static int64_t eval_eq(EvalCtx *c){
    int64_t v=eval_rel(c);
    for(;;){ skipws(c);
        if(c->s[0]=='='&&c->s[1]=='='){ c->s+=2; v=(v==eval_rel(c)); }
        else if(c->s[0]=='!'&&c->s[1]=='='){ c->s+=2; v=(v!=eval_rel(c)); }
        else break;
    }
    return v;
}
static int64_t eval_band(EvalCtx *c){ int64_t v=eval_eq(c); for(;;){ skipws(c); if(c->s[0]=='&'&&c->s[1]!='&'){ c->s++; v&=eval_eq(c);} else break; } return v; }
static int64_t eval_bxor(EvalCtx *c){ int64_t v=eval_band(c); for(;;){ skipws(c); if(*c->s=='^'){ c->s++; v^=eval_band(c);} else break; } return v; }
static int64_t eval_bor(EvalCtx *c){ int64_t v=eval_bxor(c); for(;;){ skipws(c); if(c->s[0]=='|'&&c->s[1]!='|'){ c->s++; v|=eval_bxor(c);} else break; } return v; }
static int64_t eval_land(EvalCtx *c){ int64_t v=eval_bor(c); for(;;){ skipws(c); if(c->s[0]=='&'&&c->s[1]=='&'){ c->s+=2; int64_t r=eval_bor(c); v=(v&&r);} else break; } return v; }
static int64_t eval_lor(EvalCtx *c){ int64_t v=eval_land(c); for(;;){ skipws(c); if(c->s[0]=='|'&&c->s[1]=='|'){ c->s+=2; int64_t r=eval_land(c); v=(v||r);} else break; } return v; }
static int64_t eval_expr(EvalCtx *c){ return eval_lor(c); }

static int eval_str(const char *s, int64_t *out)
{
    EvalCtx c; c.s=s; c.err=0;
    int64_t v=eval_expr(&c);
    skipws(&c);
    if(c.err || *c.s) return 0;
    *out=v; return 1;
}

static int64_t ev_const(const char *s)
{
    int64_t v=0;
    while(*s==' '||*s=='\t')s++;
    if(eval_str(s,&v)) return v;
    return 0;
}

static void split_statements(char *buf, Lines *out)
{
    char *line = buf;
    while(*line){
        char *nl = strchr(line,'\n');
        size_t linelen = nl ? (size_t)(nl-line) : strlen(line);

        char *cur = line;
        char *lineend = line+linelen;
        if(lineend>cur && lineend[-1]=='\r') lineend--;

        char *seg = cur;
        int inq=0; char q=0;
        for(char *p=cur; p<=lineend; p++){
            if(p==lineend || (!inq && *p==';')){
                size_t n=(size_t)(p-seg);
                char *s=malloc(n+1); memcpy(s,seg,n); s[n]=0;
                lines_push(out,s);
                seg=p+1;
                if(p==lineend) break;
            } else if(inq){
                if(*p=='\\' && p+1<lineend) p++;
                else if(*p==q) inq=0;
            } else if(*p=='"'||*p=='\''){ inq=1; q=*p; }
        }
        if(!nl) break;
        line = nl+1;
    }
}

static char *trim(char *s){
    while(*s==' '||*s=='\t') s++;
    char *e=s+strlen(s);
    while(e>s && (e[-1]==' '||e[-1]=='\t'||e[-1]=='\r')) e--;
    *e=0; return s;
}

typedef struct Macro {
    char *name;
    char **params; int nparams;
    char **varargIdx;
    Lines body;
    struct Macro *next;
} Macro;
static Macro *g_macros=NULL;
static Macro *macro_find(const char *n){ for(Macro *m=g_macros;m;m=m->next) if(!strcmp(m->name,n)) return m; return NULL; }

static int g_macro_ctr=0;

static int is_ident_ch(char c){ return isalnum((unsigned char)c)||c=='_'||c=='.'||c=='$'; }

static char *subst_args(const char *in, char **params, char **args, int nparams, int invno)
{
    Str o; str_init(&o);
    const char *p=in;
    int inq=0; char q=0;
    while(*p){
        if(inq){
            str_addc(&o,*p);
            if(*p=='\\' && p[1]){ p++; str_addc(&o,*p); }
            else if(*p==q) inq=0;
            p++;
            continue;
        }
        if(*p=='"'||*p=='\''){ inq=1; q=*p; str_addc(&o,*p); p++; continue; }
        if(*p=='\\'){
            if(p[1]=='@'){ char num[32]; sprintf(num,"%d",invno); str_add(&o,num); p+=2; continue; }
            if(p[1]=='('&&p[2]==')'){ p+=3; continue; }

            const char *q2=p+1; char nm[256]; int i=0;
            while(is_ident_ch(*q2) && *q2!='.'){ if(i<255)nm[i++]=*q2; q2++; }
            nm[i]=0;
            int found=-1;
            for(int k=0;k<nparams;k++) if(!strcmp(params[k],nm)){ found=k; break; }
            if(found>=0){ str_add(&o, args[found]?args[found]:""); p=q2; continue; }

            str_addc(&o,p[1]); p+=2; continue;
        }
        str_addc(&o,*p); p++;
    }
    return o.p;
}

static char *subst_symbols(const char *in)
{
    Str o; str_init(&o);
    const char *p=in;
    int inq=0; char q=0;
    while(*p){
        if(inq){ str_addc(&o,*p); if(*p=='\\'&&p[1]){p++;str_addc(&o,*p);} else if(*p==q) inq=0; p++; continue; }
        if(*p=='"'||*p=='\''){ inq=1;q=*p; str_addc(&o,*p); p++; continue; }
        if(isalpha((unsigned char)*p)||*p=='_'||*p=='.'||*p=='$'){
            const char *st=p; char nm[256]; int i=0;
            while(is_ident_ch(*p)){ if(i<255)nm[i++]=*p; p++; }
            nm[i]=0;
            Sym *s=sym_find(nm);
            if(s){ char num[32]; sprintf(num,"%lld",(long long)s->val); str_add(&o,num); }
            else str_addn(&o,st,(size_t)(p-st));
            continue;
        }
        str_addc(&o,*p); p++;
    }
    return o.p;
}

static char *mangle_label_token(const char *tok)
{
    Str o; str_init(&o);
    str_add(&o,"rL_");
    const char *p=tok; while(*p=='.') p++;
    str_add(&o,p);
    return o.p;
}

static char *rewrite_local_labels(const char *in)
{
    Str o; str_init(&o);
    const char *p=in; int inq=0; char q=0;
    while(*p){
        if(inq){ str_addc(&o,*p); if(*p=='\\'&&p[1]){p++;str_addc(&o,*p);} else if(*p==q) inq=0; p++; continue; }
        if(*p=='"'||*p=='\''){ inq=1;q=*p; str_addc(&o,*p); p++; continue; }
        if(p[0]=='.' && (p==in || !is_ident_ch(p[-1])) && (isalpha((unsigned char)p[1])||p[1]=='_')){
            char tok[256]; int i=0;
            while(is_ident_ch(*p)){ if(i<255)tok[i++]=*p; p++; }
            tok[i]=0;
            char *m=mangle_label_token(tok);
            str_add(&o,m); free(m);
            continue;
        }
        str_addc(&o,*p); p++;
    }
    return o.p;
}

static char *strip_lo12(const char *in)
{
    Str o; str_init(&o);
    const char *p=in;
    while(*p){
        if(!strncmp(p,"#:lo12:",7)){ p+=7; continue; }
        if(!strncmp(p,":lo12:",6)){ p+=6; continue; }
        if(!strncmp(p,"#:abs_g0_nc:",12)||!strncmp(p,"#:abs_g0:",9)){  }
        str_addc(&o,*p); p++;
    }
    return o.p;
}

#include "gas2armasm_emit.h"

static void expand(Lines *src, size_t from, size_t to, char **params, char **args, int nparams, int invno);

static int parse_call_args(const char *s, char ***out)
{
    char **a=malloc(sizeof(char*)*32); int n=0, cap=32;
    const char *p=s;
    while(*p==' '||*p=='\t') p++;
    if(!*p){ *out=a; return 0; }
    Str cur; str_init(&cur);
    int depth=0, inq=0; char q=0;
    for(;;){
        char c=*p;
        if(inq){ str_addc(&cur,c); if(c=='\\'&&p[1]){p++;str_addc(&cur,*p);} else if(c==q) inq=0; p++; if(!*p){ } }
        else if(c=='"'||c=='\''){ inq=1;q=c; str_addc(&cur,c); p++; }
        else if(c=='('){ depth++; str_addc(&cur,c); p++; }
        else if(c==')'){ depth--; str_addc(&cur,c); p++; }
        else if((c==','&&depth==0) || c==0){
            char *t=strdup(trim(cur.p));
            if(n==cap){cap*=2;a=realloc(a,sizeof(char*)*cap);}
            a[n++]=t;
            free(cur.p); str_init(&cur);
            if(c==0) break;
            p++;
        }
        else { str_addc(&cur,c); p++; }
    }
    *out=a; return n;
}

static size_t find_block_end(Lines *L, size_t i, const char *openkw, const char *closekw)
{
    int depth=1;
    for(size_t k=i;k<L->n;k++){
        char *line=L->v[k]; while(*line==' '||*line=='\t')line++;
        if(!strncmp(line,openkw,strlen(openkw)) && (line[strlen(openkw)]==' '||line[strlen(openkw)]=='\t'||line[strlen(openkw)]==0))
            depth++;
        else if(!strncmp(line,closekw,strlen(closekw)) && (line[strlen(closekw)]==0||line[strlen(closekw)]==' '||line[strlen(closekw)]=='\t'||line[strlen(closekw)]=='\r')){
            depth--;
            if(depth==0) return k;
        }
    }
    fail("unterminated block");
    return 0;
}

static void handle_if(Lines *L, size_t *pi, char **params, char **args, int nparams, int invno)
{
    size_t i=*pi;

    int depth=1;
    size_t start=i+1;

    char *line=L->v[i]; while(*line==' '||*line=='\t')line++;
    const char *cond = line+3;
    char *ca = subst_args(cond, params, args, nparams, invno);
    char *sc = subst_symbols(ca);
    int64_t cv; int taken = eval_str(trim(sc),&cv) ? (cv!=0) : 0;
    free(sc); free(ca);
    int any_taken=0;
    size_t seg_start=start;
    size_t k=start;
    int active = taken;
    for(; k<L->n; k++){
        char *ln=L->v[k]; while(*ln==' '||*ln=='\t')ln++;
        int isif = (!strncmp(ln,".if",3) && (ln[3]==' '||ln[3]=='\t'));
        int isendif = (!strncmp(ln,".endif",6) && (ln[6]==0||ln[6]=='\r'||ln[6]==' '));
        int iselse = (!strncmp(ln,".else",5) && (ln[5]==0||ln[5]=='\r'||ln[5]==' '||ln[5]=='i'));
        int iselseif = (!strncmp(ln,".elseif",7));
        if(isif) depth++;
        else if(isendif){ depth--; if(depth==0){
            if(active && !any_taken){ expand(L,seg_start,k,params,args,nparams,invno); }
            *pi=k; return; } }
        else if(depth==1 && iselseif){
            if(active && !any_taken){ expand(L,seg_start,k,params,args,nparams,invno); any_taken=1; }
            const char *c2=ln+7; char *a2=subst_args(c2,params,args,nparams,invno); char *s2=subst_symbols(a2); int64_t v2; int t2= eval_str(trim(s2),&v2)?(v2!=0):0; free(s2); free(a2);
            active = (!any_taken) && t2; if(active) {}
            seg_start=k+1;
        }
        else if(depth==1 && iselse){
            if(active && !any_taken){ expand(L,seg_start,k,params,args,nparams,invno); any_taken=1; }
            active = (!any_taken);
            seg_start=k+1;
        }
    }
    fail("unterminated .if");
}

static void expand(Lines *src, size_t from, size_t to, char **params, char **args, int nparams, int invno)
{
    for(size_t i=from;i<to;i++){

        char *stmt = subst_args(src->v[i], params, args, nparams, invno);
        char *t = trim(stmt);
        if(*t==0){ free(stmt); continue; }

        char first[64]; int fi=0;
        const char *q=t; while(*q && !isspace((unsigned char)*q) && fi<63) first[fi++]=*q++;
        first[fi]=0;

        if(!strcmp(first,".macro")){

            const char *rest=t+6; while(*rest==' '||*rest=='\t')rest++;
            Macro *m=calloc(1,sizeof(Macro));

            char nm[128]; int ni=0; while(*rest && !isspace((unsigned char)*rest) && *rest!=',' && ni<127) nm[ni++]=*rest++;
            nm[ni]=0; m->name=strdup(nm);
            m->params=malloc(sizeof(char*)*32); m->nparams=0;

            while(*rest){
                while(*rest==' '||*rest=='\t'||*rest==',') rest++;
                if(!*rest) break;
                char pn[128]; int pj=0;
                while(*rest && *rest!=' '&&*rest!='\t'&&*rest!=','&&*rest!=':'&&*rest!='=' && pj<127) pn[pj++]=*rest++;
                pn[pj]=0;

                while(*rest && *rest!=','&&*rest!=' '&&*rest!='\t') rest++;
                if(pj) m->params[m->nparams++]=strdup(pn);
            }
            lines_init(&m->body);
            size_t j=i+1; int depth=1;
            for(; j<src->n; j++){
                char *ln=src->v[j]; while(*ln==' '||*ln=='\t')ln++;
                if(!strncmp(ln,".macro",6)&&(ln[6]==' '||ln[6]=='\t')) depth++;
                else if(!strncmp(ln,".endm",5)&&(ln[5]==0||ln[5]=='\r'||ln[5]==' ')){ depth--; if(depth==0) break; }
                lines_push(&m->body, strdup(src->v[j]));
            }
            m->next=g_macros; g_macros=m;
            i=j;
            free(stmt); continue;
        }
        if(!strcmp(first,".rept")||!strcmp(first,".irp")||!strcmp(first,".irpc")){
            if(strcmp(first,".rept")) fail(".irp/.irpc not supported");
            char *cnts=subst_symbols(t+5); int64_t cnt; if(!eval_str(trim(cnts),&cnt)){ free(cnts); fail("bad .rept count"); } free(cnts);
            size_t endr=find_block_end(src,i+1,".rept",".endr");
            for(int64_t r=0;r<cnt;r++) expand(src,i+1,endr,params,args,nparams,invno);
            i=endr; free(stmt); continue;
        }
        if(!strcmp(first,".if")||!strcmp(first,".ifdef")||!strcmp(first,".ifndef")||!strcmp(first,".ifb")||!strcmp(first,".ifnb")||!strcmp(first,".ifc")||!strcmp(first,".ifnc")){
            if(strcmp(first,".if")) fail(".ifX variants not supported");
            handle_if(src,&i,params,args,nparams,invno);
            free(stmt); continue;
        }
        if(!strcmp(first,".set")||!strcmp(first,".equ")||!strcmp(first,".equiv")){
            const char *rest=t+strlen(first); while(*rest==' '||*rest=='\t')rest++;
            char nm[256]; int ni=0; while(*rest && *rest!=',' && !isspace((unsigned char)*rest) && ni<255) nm[ni++]=*rest++;
            nm[ni]=0; while(*rest==' '||*rest=='\t')rest++; if(*rest==',')rest++;
            char *rs=subst_symbols(rest); int64_t v;
            if(eval_str(trim(rs),&v)) sym_set(nm,v);
            else emit_equ(nm, rest);
            free(rs); free(stmt); continue;
        }

        {

            const char *p=t; char nm[256]; int ni=0;
            if(isalpha((unsigned char)*p)||*p=='_'||*p=='.'){
                while(is_ident_ch(*p)&&ni<255) nm[ni++]=*p++;
                nm[ni]=0;
                const char *r=p; while(*r==' '||*r=='\t')r++;
                if(*r=='=' && r[1]!='='){
                    r++;
                    char *rs=subst_symbols(r); int64_t v;
                    if(eval_str(trim(rs),&v)){ sym_set(nm,v); free(rs); free(stmt); continue; }
                    free(rs);
                    emit_equ(nm, r);
                    free(stmt); continue;
                }
            }
        }
        if(!strcmp(first,".error")){ fprintf(stderr,"gas2armasm: .error: %s\n", t); exit(1); }

        Macro *m=macro_find(first);
        if(m){
            const char *rest=t+strlen(first); while(*rest==' '||*rest=='\t')rest++;
            char **cargs; int nc=parse_call_args(rest,&cargs);
            int inv=++g_macro_ctr;

            char **full=malloc(sizeof(char*)*(m->nparams>nc?m->nparams:nc));
            for(int k=0;k<m->nparams;k++) full[k] = (k<nc)?cargs[k]:(char*)"";
            expand(&m->body,0,m->body.n, m->params, full, m->nparams, inv);
            free(full);
            for(int k=0;k<nc;k++) free(cargs[k]);
            free(cargs);
            free(stmt); continue;
        }

        translate_stmt(t);
        free(stmt);
    }
}

int main(int argc, char **argv)
{
    const char *inpath=NULL, *outpath=NULL;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"-o") && i+1<argc) outpath=argv[++i];
        else if(argv[i][0] != '-') { if(!inpath) inpath=argv[i]; }
    }
    FILE *in = inpath ? fopen(inpath,"rb") : stdin;
    if(!in){ fprintf(stderr,"gas2armasm: cannot open %s\n", inpath); return 1; }
    if(inpath) g_infile=inpath;

    Str buf; str_init(&buf);
    char tmp[65536]; size_t r;
    while((r=fread(tmp,1,sizeof(tmp),in))>0) str_addn(&buf,tmp,r);
    if(inpath) fclose(in);

    Lines L; lines_init(&L);
    split_statements(buf.p,&L);

    int maxalign = 4;
    for(size_t i=0;i<L.n;i++){
        char *ln=L.v[i]; while(*ln==' '||*ln=='\t')ln++;
        int e=-1;
        if(!strncmp(ln,".p2align",8)) e=(int)ev_const(ln+8);
        else if(!strncmp(ln,".align",6)) e=(int)ev_const(ln+6);
        else if(!strncmp(ln,".balign",7)){ int64_t b=ev_const(ln+7); e=0; while((1LL<<e)<b) e++; }
        if(e>maxalign) maxalign=e;
    }

    emit_init(maxalign);
    expand(&L,0,L.n,NULL,NULL,0,0);
    emit_finish();

    FILE *out = outpath ? fopen(outpath,"wb") : stdout;
    if(!out){ fprintf(stderr,"gas2armasm: cannot open %s\n", outpath); return 1; }
    emit_write(out);
    if(outpath) fclose(out);
    return 0;
}
