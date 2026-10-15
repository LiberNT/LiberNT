#!/usr/bin/env python3
#
# PROJECT:     LiberNT Build Tools
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Find build graph gaps on generated, included and embedded files
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

import argparse
import collections
import concurrent.futures
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

INCLUDE_RE = re.compile(rb'^[ \t]*#[ \t]*(?:include|import|include_next)[ \t]*([<"])([^>"\r\n]+)[>"]', re.M)
RESOURCE_RE = re.compile(rb'"([^"\s]{1,240}\.(?:ico|cur|ani|bmp|png|jpe?g|gif|svg|avi|wav|mid|manifest|xml|xsl|html?|css|js|bin|dat|ttf|otf|fon|mof|tlb|rgs|wmf|emf|cab|msi))"', re.I)
COMPILE_RULE_PREFIXES = ('C_COMPILER', 'CXX_COMPILER', 'ASM_COMPILER', 'RC_COMPILER', 'OBJC_COMPILER', 'OBJCXX_COMPILER')
INCLUDE_FLAGS = ('-I', '-isystem', '-iquote', '-idirafter', '--include-dir')
LINEMARKER_RE = re.compile(rb'^#(?:line)?[ \t]+[0-9]+[ \t]+"((?:[^"\\\\]|\\\\.)*)"', re.M)
VAR_RE = re.compile(r'\$\{([A-Za-z0-9_.]+)\}|\$([A-Za-z0-9_]+)')
SHELL_OPERATORS = frozenset(('&&', '||', ';', '|', '>', '>>', '<', '&', '`'))
OUTPUT_FLAGS = ('-MD', '-MMD', '-save-temps', '-fdump-', '-gsplit-dwarf', '-Wp,-MD', '-Wp,-MMD', '--save-temps', '-ftime-trace', '-MJ', '-fmodules-cache-path', '-fcrash-diagnostics-dir')
EXIT_CLEAN, EXIT_GAPS, EXIT_ERROR = 0, 1, 2


class DepcheckError(Exception):
    pass


def unescape(token):
    out = []
    i = 0
    while i < len(token):
        if token[i] == '$' and i + 1 < len(token):
            out.append(token[i + 1])
            i += 2
            continue
        out.append(token[i])
        i += 1
    return ''.join(out)


def split_tokens(text):
    tokens = []
    cur = []
    i = 0
    while i < len(text):
        c = text[i]
        if c == '$' and i + 1 < len(text):
            cur.append(text[i:i + 2])
            i += 2
            continue
        if c in ' \t':
            if cur:
                tokens.append(''.join(cur))
                cur = []
        else:
            cur.append(c)
        i += 1
    if cur:
        tokens.append(''.join(cur))
    return tokens


def find_unescaped_colon(text):
    i = 0
    while i < len(text):
        if text[i] == '$':
            i += 2
            continue
        if text[i] == ':':
            return i
        i += 1
    return -1


class Edge:
    __slots__ = ('outs', 'implicit_outs', 'rule', 'ins', 'implicit_ins', 'order_ins', 'vars')

    def __init__(self):
        self.outs = []
        self.implicit_outs = []
        self.rule = ''
        self.ins = []
        self.implicit_ins = []
        self.order_ins = []
        self.vars = {}

    def inputs(self):
        return self.ins + self.implicit_ins + self.order_ins


class Graph:
    def __init__(self, build_dir):
        self.build_dir = os.path.abspath(build_dir)
        self.globals = {}
        self.rules = {}
        self.edges = []
        self.producer = {}
        self.parsed = set()
        self.warnings = []
        self.defaults = []
        self.parse(os.path.join(self.build_dir, 'build.ninja'))
        for edge in self.edges:
            for out in edge.outs + edge.implicit_outs:
                self.producer[out] = edge
        self.default_nodes = self.closure(self.defaults or [o for e in self.edges for o in e.outs])

    def closure(self, roots):
        seen = set()
        pending = list(roots)
        while pending:
            node = pending.pop()
            if node in seen:
                continue
            seen.add(node)
            edge = self.producer.get(node)
            if edge is not None:
                pending.extend(dep for dep in edge.inputs() if dep not in seen)
        return seen

    def built_by_default(self, edge):
        return any(out in self.default_nodes for out in edge.outs)

    def path(self, token):
        token = unescape(token)
        workdir = self.globals.get('cmake_ninja_workdir', self.build_dir + '/')
        token = token.replace('${cmake_ninja_workdir}', workdir).replace('$cmake_ninja_workdir', workdir)
        if not os.path.isabs(token):
            token = os.path.join(self.build_dir, token)
        return os.path.normpath(token)

    def parse(self, filename):
        filename = os.path.normpath(filename)
        if filename in self.parsed:
            return
        self.parsed.add(filename)
        try:
            with open(filename, encoding='utf-8', errors='surrogateescape') as stream:
                text = stream.read()
        except OSError as error:
            if not self.edges:
                raise DepcheckError('cannot read %s: %s' % (filename, error.strerror))
            self.warnings.append('cannot read included %s: %s' % (filename, error.strerror))
            return
        text = re.sub(r'\$\r?\n[ \t]*', '', text).replace('\r\n', '\n')
        current = None
        for number, line in enumerate(text.split('\n'), 1):
            if not line or line.startswith('#'):
                continue
            if line.startswith('build '):
                current = self.parse_build(line[6:])
                if current is None:
                    self.warnings.append('%s:%d: build line without a rule separator' % (filename, number))
                    continue
                self.edges.append(current)
            elif line.startswith('rule '):
                current = {}
                self.rules[line[5:].strip()] = current
            elif line.startswith(('include ', 'subninja ')):
                current = None
                target = line.split(None, 1)[1].strip()
                if not os.path.isabs(target):
                    target = os.path.join(self.build_dir, target)
                self.parse(target)
            elif line[0] in ' \t':
                key, sep, value = line.strip().partition('=')
                if not sep or current is None:
                    continue
                if isinstance(current, Edge):
                    current.vars[key.strip()] = value.strip()
                else:
                    current[key.strip()] = value.strip()
            elif line.startswith('default '):
                current = None
                self.defaults.extend(self.path(token) for token in split_tokens(line[8:]))
            else:
                current = None
                key, sep, value = line.partition('=')
                if sep and not key.strip().startswith('pool'):
                    self.globals[key.strip()] = value.strip()

    def parse_build(self, text):
        edge = Edge()
        colon = find_unescaped_colon(text)
        if colon < 0:
            return None
        left, right = text[:colon], text[colon + 1:]
        target = edge.outs
        for token in split_tokens(left):
            if token == '|':
                target = edge.implicit_outs
                continue
            target.append(self.path(token))
        tokens = split_tokens(right)
        if tokens:
            edge.rule = tokens[0]
        target = edge.ins
        for token in tokens[1:]:
            if token == '|':
                target = edge.implicit_ins
            elif token == '||':
                target = edge.order_ins
            elif token == '|@':
                target = []
            else:
                target.append(self.path(token))
        return edge

    def command(self, edge, extra=None):
        values = dict(self.globals)
        values.update(edge.vars)
        values['in'] = ' '.join(shlex.quote(p) for p in edge.ins)
        values['out'] = shlex.quote(edge.outs[0]) if edge.outs else ''
        values['LAUNCHER'] = ''
        values['CODE_CHECK'] = ''
        if extra:
            values.update(extra)
        template = self.rules.get(edge.rule, {}).get('command', '').replace('$$', '\0')
        expanded = VAR_RE.sub(lambda m: values.get(m.group(1) or m.group(2), ''), template)
        return expanded.replace('\0', '$')


class Reachability:
    def __init__(self, graph, wanted):
        self.graph = graph
        self.bit = {path: 1 << index for index, path in enumerate(sorted(wanted))}
        self.memo = {}

    def of_node(self, node):
        memo = self.memo
        if node in memo:
            return memo[node]
        active = set()
        stack = [(node, False)]
        while stack:
            current, expanded = stack.pop()
            if current in memo:
                continue
            edge = self.graph.producer.get(current)
            if not expanded:
                if current in active:
                    continue
                active.add(current)
                stack.append((current, True))
                if edge is not None:
                    stack.extend((dep, False) for dep in edge.inputs() if dep not in memo and dep not in active)
                continue
            active.discard(current)
            mask = self.bit.get(current, 0)
            if edge is not None:
                for dep in edge.inputs():
                    mask |= memo.get(dep, 0)
            memo[current] = mask
        return memo[node]

    def of_edge(self, edge):
        mask = 0
        for dep in edge.inputs():
            mask |= self.of_node(dep)
        return mask


def search_dirs(edge, build_dir):
    quote_dirs, dirs, forced = [], [], []
    text = ' '.join(edge.vars.get(key, '') for key in ('INCLUDES', 'FLAGS'))
    try:
        tokens = shlex.split(text)
    except ValueError:
        tokens = text.split()
    i = 0
    while i < len(tokens):
        token = tokens[i]
        if token in ('-include', '-imacros') and i + 1 < len(tokens):
            forced.append(os.path.normpath(os.path.join(build_dir, tokens[i + 1])))
            i += 2
            continue
        for flag in INCLUDE_FLAGS:
            if token == flag and i + 1 < len(tokens):
                value = tokens[i + 1]
                i += 1
            elif token.startswith(flag) and len(token) > len(flag):
                value = token[len(flag):].lstrip('=')
            else:
                continue
            path = os.path.normpath(os.path.join(build_dir, value))
            (quote_dirs if flag == '-iquote' else dirs).append(path)
            break
        i += 1
    return tuple(quote_dirs), tuple(dirs), tuple(forced)


class Resolver:
    def __init__(self, generated):
        self.generated = generated
        self.memo = {}

    def exists(self, path):
        return path in self.generated or os.path.isfile(path)

    def resolve(self, name, quoted, local_dirs, quote_dirs, dirs):
        name = name.replace('\\', '/')
        if os.path.isabs(name):
            path = os.path.normpath(name)
            return path if self.exists(path) else None
        for base in ((tuple(local_dirs) + quote_dirs + dirs) if quoted else dirs):
            candidate = os.path.normpath(os.path.join(base, name))
            if self.exists(candidate):
                return candidate
        return None


class StaticScanner:
    def __init__(self, resolver):
        self.resolver = resolver
        self.direct_memo = {}
        self.closure_memo = {}

    def direct(self, path, root_dir, quote_dirs, dirs, resources):
        key = (path, root_dir if resources else None, quote_dirs, dirs, resources)
        cached = self.direct_memo.get(key)
        if cached is not None:
            return cached
        includes, embedded, missing = [], [], []
        try:
            with open(path, 'rb') as stream:
                data = stream.read()
        except OSError:
            data = b''
        here = os.path.dirname(path)
        for match in INCLUDE_RE.finditer(data):
            name = match.group(2).decode('utf-8', 'surrogateescape')
            target = self.resolver.resolve(name, match.group(1) == b'"', (here,), quote_dirs, dirs)
            if target is not None:
                includes.append(target)
        if resources and path.lower().endswith(('.rc', '.rc2', '.dlg')):
            for match in RESOURCE_RE.finditer(data):
                name = match.group(1).decode('utf-8', 'surrogateescape')
                target = self.resolver.resolve(name, True, (here, root_dir), quote_dirs, dirs)
                if target is None:
                    missing.append((path, name))
                else:
                    embedded.append(target)
        cached = (tuple(includes), tuple(embedded), tuple(missing))
        self.direct_memo[key] = cached
        return cached

    def scan(self, start, quote_dirs, dirs, resources):
        key = (start, quote_dirs, dirs, resources)
        cached = self.closure_memo.get(key)
        if cached is not None:
            return cached
        root_dir = os.path.dirname(start)
        seen = set()
        reads, embedded, missing = set(), set(), set()
        pending = [start]
        while pending:
            path = pending.pop()
            if path in seen:
                continue
            seen.add(path)
            reads.add(path)
            if not os.path.isfile(path):
                continue
            includes, emb, miss = self.direct(path, root_dir, quote_dirs, dirs, resources)
            pending.extend(t for t in includes if t not in seen)
            embedded.update(emb)
            missing.update(miss)
        cached = (frozenset(reads), frozenset(embedded), frozenset(missing))
        self.closure_memo[key] = cached
        return cached


def parse_make_deps(text):
    text = text.replace('\\\n', ' ').replace('\\ ', '\0')
    colon = text.find(': ')
    if colon < 0:
        colon = text.find(':\n')
    body = text[colon + 1:] if colon >= 0 else ''
    return [token.replace('\0', ' ') for token in body.split()]


def strip_output_flags(tokens, with_value):
    cleaned = []
    skip = False
    for token in tokens:
        if skip:
            skip = False
            continue
        if token in with_value:
            skip = True
            continue
        if token in ('-c', '-E') or token.startswith(OUTPUT_FLAGS):
            continue
        if token.startswith(('-MF', '-MT', '-MQ')) and len(token) > 3:
            continue
        if token.startswith('-o') and len(token) > 2:
            continue
        cleaned.append(token)
    return cleaned


def preprocessor_command(graph, edge, depfile):
    try:
        tokens = shlex.split(graph.command(edge))
    except ValueError as error:
        raise DepcheckError('cannot split the command: %s' % error)
    if not tokens:
        raise DepcheckError('empty command')
    if SHELL_OPERATORS.intersection(tokens):
        raise DepcheckError('command runs more than one program')
    if edge.rule.startswith('RC_COMPILER'):
        program, pre_args, defines = None, [], []
        index = 0
        while index < len(tokens):
            token = tokens[index]
            if token.startswith('--preprocessor='):
                program = token.split('=', 1)[1]
            elif token.startswith('--preprocessor-arg='):
                pre_args.append(token.split('=', 1)[1])
            elif token in ('-D', '-U', '-I', '--include-dir', '--define', '--undefine') and index + 1 < len(tokens):
                flag = {'--include-dir': '-I', '--define': '-D', '--undefine': '-U'}.get(token, token)
                defines.append(flag + tokens[index + 1])
                index += 1
            elif token.startswith(('--include-dir=', '--define=', '--undefine=')):
                name, value = token.split('=', 1)
                defines.append({'--include-dir': '-I', '--define': '-D', '--undefine': '-U'}[name] + value)
            elif token.startswith(('-D', '-U', '-I')) and len(token) > 2:
                defines.append(token)
            index += 1
        if not program:
            raise DepcheckError('resource compiler without a --preprocessor')
        argv = shlex.split(program) + strip_output_flags(pre_args, ('-MT', '-MF', '-MQ', '-o', '-MJ')) + defines
    else:
        argv = strip_output_flags(tokens, ('-MT', '-MF', '-MQ', '-o', '-MJ'))
    executable = argv[0]
    if not (os.path.isabs(executable) and os.access(executable, os.X_OK)) and not shutil.which(executable):
        raise DepcheckError('compiler not found: %s' % executable)
    argv = argv + ['-E', '-M', '-MG', '-MF', depfile]
    if edge.rule.startswith('RC_COMPILER'):
        argv.append(edge.ins[0])
    return argv


def run_preprocessor(graph, argv, timeout):
    proc = subprocess.run(argv, cwd=graph.build_dir, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=timeout)
    if proc.returncode != 0:
        lines = [l for l in proc.stderr.decode('utf-8', 'replace').splitlines() if 'error' in l] or \
                proc.stderr.decode('utf-8', 'replace').strip().splitlines()
        raise DepcheckError(lines[0][:300] if lines else 'preprocessor exited with %d' % proc.returncode)
    return proc.stdout


def resource_references(text, main_file):
    current = main_file
    references = []
    position = 0
    for marker in LINEMARKER_RE.finditer(text):
        chunk = text[position:marker.start()]
        references.extend((current, m.group(1).decode('utf-8', 'surrogateescape')) for m in RESOURCE_RE.finditer(chunk))
        current = marker.group(1).decode('utf-8', 'surrogateescape').replace('\\\\', '\\')
        position = marker.end()
    references.extend((current, m.group(1).decode('utf-8', 'surrogateescape')) for m in RESOURCE_RE.finditer(text[position:]))
    return references


def compiler_reads(graph, edge, tmpdir, index, timeout):
    depfile = os.path.join(tmpdir, '%d.d' % index)
    try:
        argv = preprocessor_command(graph, edge, depfile)
        run_preprocessor(graph, argv, timeout)
        with open(depfile, encoding='utf-8', errors='surrogateescape') as stream:
            deps = parse_make_deps(stream.read())
        references = None
        if edge.rule.startswith('RC_COMPILER'):
            text_argv = [a for a in argv if a not in ('-M', '-MG')]
            cut = text_argv.index('-MF')
            del text_argv[cut:cut + 2]
            references = resource_references(run_preprocessor(graph, text_argv, timeout), edge.ins[0])
    except DepcheckError as error:
        return None, str(error)
    except subprocess.TimeoutExpired:
        return None, 'preprocessor timed out after %d s' % timeout
    except OSError as error:
        return None, 'cannot run the preprocessor: %s' % (error.strerror or error)
    finally:
        try:
            os.unlink(depfile)
        except OSError:
            pass
    return (deps, references), None


def target_of(obj, build_dir):
    rel = os.path.relpath(obj, build_dir)
    match = re.search(r'(.*?)/?CMakeFiles/([^/]+)\.dir/', rel)
    if match:
        return match.group(2), match.group(1) or '.'
    return rel, os.path.dirname(rel)


def owners_of(graph):
    owner = collections.defaultdict(set)
    for edge in graph.edges:
        if edge.rule != 'phony':
            continue
        for out in edge.outs:
            match = re.search(r'(?:^|/)CMakeFiles/([^/]+)$', os.path.relpath(out, graph.build_dir))
            if match:
                for dep in edge.ins + edge.implicit_ins:
                    owner[dep].add(match.group(1))
    return owner


def undeclared_command_inputs(graph, source_dir):
    findings = []
    for edge in graph.edges:
        if not edge.rule.startswith('CUSTOM_COMMAND'):
            continue
        declared = set(edge.inputs())
        for raw in re.split(r'[\s;&|<>()]+', edge.vars.get('COMMAND', '')):
            token = raw.strip('"\'')
            if '=' in token and not token.startswith('/'):
                token = token.split('=', 1)[1]
            if not token.startswith('/'):
                continue
            path = os.path.normpath(token)
            if path in declared or not path.startswith(source_dir + os.sep):
                continue
            if path.startswith(graph.build_dir + os.sep) or not os.path.isfile(path):
                continue
            findings.append((edge.outs[0] if edge.outs else '', path))
    return findings


def recorded_deps(build_dir):
    proc = subprocess.run(['ninja', '-C', build_dir, '-t', 'deps'], capture_output=True, text=True, errors='surrogateescape')
    deps = collections.defaultdict(list)
    target = None
    for line in proc.stdout.splitlines():
        if not line.strip():
            continue
        if not line.startswith(' '):
            target = os.path.normpath(os.path.join(build_dir, line.split(':', 1)[0]))
        elif target:
            deps[target].append(line.strip())
    return deps


def analyze(build_dir, source_dir, mode, jobs, progress, timeout, all_targets=False):
    graph = Graph(build_dir)
    compile_edges = [e for e in graph.edges if e.rule.startswith(COMPILE_RULE_PREFIXES) and e.ins and e.outs]
    generated = {path for path, edge in graph.producer.items() if edge.rule != 'phony'}
    resolver = Resolver(generated)
    scanner = StaticScanner(resolver)
    owner = owners_of(graph)

    reads = {}
    references = {}
    failures = []
    pending_sources = []
    if mode == 'depfiles':
        recorded = recorded_deps(build_dir)
        for edge in compile_edges:
            if edge.outs[0] in recorded:
                reads[id(edge)] = recorded[edge.outs[0]]
    elif mode == 'compiler':
        with tempfile.TemporaryDirectory(prefix='depcheck') as tmpdir:
            pool = concurrent.futures.ThreadPoolExecutor(max_workers=max(1, jobs))
            try:
                runnable = [edge for edge in compile_edges if os.path.exists(edge.ins[0]) or edge.ins[0] not in generated]
                pending_sources.extend(edge.outs[0] for edge in compile_edges if edge not in runnable)
                futures = {pool.submit(compiler_reads, graph, edge, tmpdir, index, timeout): edge for index, edge in enumerate(runnable)}
                done = 0
                for future in concurrent.futures.as_completed(futures):
                    edge = futures[future]
                    try:
                        deps, error = future.result()
                    except Exception as exception:
                        deps, error = None, 'internal error: %r' % exception
                    if deps is None:
                        failures.append((edge.outs[0], error))
                    else:
                        reads[id(edge)] = deps[0]
                        if deps[1] is not None:
                            references[id(edge)] = deps[1]
                    done += 1
                    if progress and done % 1000 == 0:
                        print('  %d/%d steps' % (done, len(compile_edges)), file=sys.stderr)
            except KeyboardInterrupt:
                pool.shutdown(wait=False, cancel_futures=True)
                raise
            pool.shutdown(wait=True)

    records = []
    missing_includes = collections.defaultdict(set)
    missing_resources = set()
    for edge in compile_edges:
        source = edge.ins[0]
        quote_dirs, dirs, forced = search_dirs(edge, graph.build_dir)
        is_rc = edge.rule.startswith('RC_COMPILER')
        used = set()
        exact = mode != 'static' and id(edge) in reads
        if mode == 'depfiles' and not exact:
            continue
        if not exact:
            for start in (source,) + forced:
                found, _, _ = scanner.scan(start, quote_dirs, dirs, False)
                used |= found & generated
        else:
            for token in reads[id(edge)]:
                path = os.path.normpath(token if os.path.isabs(token) else os.path.join(graph.build_dir, token))
                if path in generated:
                    used.add(path)
                elif not os.path.exists(path):
                    target = resolver.resolve(token, True, (os.path.dirname(source),), quote_dirs, dirs)
                    if target in generated:
                        used.add(target)
                    elif target is None and (all_targets or graph.built_by_default(edge)):
                        missing_includes[target_of(edge.outs[0], graph.build_dir)].add((os.path.relpath(source, source_dir), token))
        embedded = set()
        if is_rc and id(edge) in references:
            for origin, name in references[id(edge)]:
                origin_path = os.path.normpath(os.path.join(graph.build_dir, origin))
                target = resolver.resolve(name, True, (os.path.dirname(origin_path), os.path.dirname(source)), quote_dirs, dirs)
                if target is None:
                    missing_resources.add((origin_path, name))
                else:
                    embedded.add(target)
            used |= embedded & generated
        elif is_rc:
            _, embedded, missing = scanner.scan(source, quote_dirs, dirs, True)
            if mode == 'static':
                missing_resources |= {(path, name + ' [static]') for path, name in missing}
            used |= embedded & generated
        used.discard(source)
        if used or embedded:
            records.append((edge, source, used, embedded, is_rc, reads.get(id(edge), ()), exact))

    reach = Reachability(graph, {p for r in records for p in r[2]})
    gaps = collections.defaultdict(lambda: collections.defaultdict(set))
    optional = set()
    blocking = set()
    untracked = collections.defaultdict(set)
    for edge, source, used, embedded, is_rc, deps, exact in records:
        key = target_of(edge.outs[0], graph.build_dir)
        if not graph.built_by_default(edge):
            optional.add(key)
        if used:
            mask = reach.of_edge(edge)
            for path in used:
                if not mask & reach.bit[path]:
                    gaps[key][path].add(os.path.relpath(source, source_dir) + ('' if exact else ' [static]'))
                    if exact and graph.built_by_default(edge):
                        blocking.add(key)
        if is_rc and embedded:
            declared = set(edge.ins + edge.implicit_ins)
            declared |= {os.path.normpath(d if os.path.isabs(d) else os.path.join(graph.build_dir, d)) for d in deps}
            for path in embedded:
                if path not in declared and path not in generated:
                    untracked[key].add(path)

    suggestions = {}
    for key, files in gaps.items():
        wanted = 0
        for path in files:
            wanted |= reach.bit[path]
        candidates = {}
        for path in files:
            for name in owner.get(path, ()):
                node = os.path.join(graph.build_dir, name)
                if node in graph.producer:
                    candidates[name] = reach.of_node(node) & wanted
        chosen = []
        left = wanted
        while left and candidates:
            name = max(sorted(candidates), key=lambda n: bin(candidates[n] & left).count('1'))
            gain = candidates.pop(name) & left
            if not gain:
                break
            chosen.append(name)
            left &= ~gain
        suggestions[key] = (chosen, left == 0)

    return {
        'graph': graph,
        'compiles': len(compile_edges),
        'gaps': gaps,
        'suggestions': suggestions,
        'owner': owner,
        'untracked': untracked,
        'missing_includes': missing_includes,
        'missing_resources': missing_resources,
        'commands': undeclared_command_inputs(graph, source_dir),
        'failures': failures,
        'warnings': graph.warnings,
        'optional': optional,
        'blocking': blocking,
        'pending_sources': pending_sources,
    }


def report(build_dir, source_dir, result, as_json, verbose):
    rel = lambda p: os.path.relpath(p, source_dir) if p.startswith(source_dir + os.sep) else p
    owner = result['owner']
    if as_json:
        print(json.dumps({
            'build_dir': build_dir,
            'compile_steps': result['compiles'],
            'ordering_gaps': [{'target': t, 'dir': d, 'file': rel(g), 'generator_targets': sorted(owner.get(g, ())), 'suggested_dependencies': result['suggestions'].get((t, d), ([], False))[0], 'sources': sorted(s), 'built_by_default': (t, d) not in result['optional']}
                              for (t, d), files in sorted(result['gaps'].items()) for g, s in sorted(files.items())],
            'missing_includes': [{'target': t, 'dir': d, 'source': s, 'include': i} for (t, d), items in sorted(result['missing_includes'].items()) for s, i in sorted(items)],
            'missing_resources': [{'file': rel(f), 'reference': r} for f, r in sorted(result['missing_resources'])],
            'untracked_resources': [{'target': t, 'dir': d, 'files': sorted(rel(p) for p in files)} for (t, d), files in sorted(result['untracked'].items())],
            'undeclared_command_inputs': [{'output': rel(o), 'input': rel(i)} for o, i in sorted(result['commands'])],
            'failed_steps': [{'output': rel(o), 'error': str(e)} for o, e in sorted(result['failures'])],
            'warnings': result['warnings'],
        }, indent=1))
        return
    gaps = result['gaps']
    print('%s: %d compile steps' % (build_dir, result['compiles']))
    print('\nOrdering gaps, a step reads a generated file without depending on its generator: %d targets' % len(gaps))
    for (target, directory), files in sorted(gaps.items()):
        chosen, complete = result['suggestions'].get((target, directory), ([], False))
        generators = chosen or sorted({o for g in files for o in owner.get(g, ())}) or ['<no utility target>']
        note = ' [not built by default]' if (target, directory) in result['optional'] else ''
        if not complete:
            note += ' [some files have no utility target]'
        print('  %s (%s): add_dependencies(%s %s)%s' % (target, directory, target, ' '.join(generators), note))
        for path, sources in sorted(files.items()):
            shown = sorted(sources)
            print('      %s  <- %s%s' % (rel(path), ', '.join(shown[:3]), ' ...' if len(shown) > 3 else ''))
    missing = result['missing_includes']
    print('\nIncludes found neither in the tree nor among generated files: %d targets' % len(missing))
    for (target, directory), items in sorted(missing.items()):
        print('  %s (%s): %s' % (target, directory, ', '.join('%s:%s' % item for item in sorted(items)[:4])))
    print('\nResource files referenced by .rc scripts and not found: %d' % len(result['missing_resources']))
    for path, name in sorted(result['missing_resources'])[:60]:
        print('  %s: "%s"' % (rel(path), name))
    untracked = result['untracked']
    print('\nEmbedded resource files the graph does not track, so editing them does not rebuild the .res: %d files in %d targets'
          % (sum(len(v) for v in untracked.values()), len(untracked)))
    if verbose:
        for (target, directory), files in sorted(untracked.items()):
            print('  %s: %s' % (target, ', '.join(sorted(rel(p) for p in files))))
    print('\nCustom commands reading source files they do not declare: %d' % len(result['commands']))
    for out, path in sorted(result['commands'])[:200 if verbose else 40]:
        print('  %s  reads  %s' % (os.path.relpath(out, build_dir), rel(path)))
    if result['failures']:
        print('\nSteps the preprocessor could not run, checked with the static scan instead ([static]): %d' % len(result['failures']))
        for out, error in sorted(result['failures'])[:20]:
            print('  %s: %s' % (os.path.relpath(out, build_dir), error))
    if result['pending_sources']:
        print('\nSteps whose source is generated and not built yet, checked with the static scan: %d' % len(result['pending_sources']))
    for warning in result['warnings'][:20]:
        print('warning: %s' % warning)


def self_test():
    with tempfile.TemporaryDirectory(prefix='depcheck-selftest') as root:
        source = os.path.join(root, 'src')
        build = os.path.join(root, 'build')
        os.makedirs(os.path.join(source, 'lib'))
        os.makedirs(os.path.join(build, 'gen'))
        with open(os.path.join(source, 'lib', 'a.c'), 'w') as stream:
            stream.write('#include "gen.h"\nint a;\n')
        with open(os.path.join(source, 'lib', 'b.c'), 'w') as stream:
            stream.write('#include "gen.h"\nint b;\n')
        with open(os.path.join(source, 'lib', 'r.rc'), 'w') as stream:
            stream.write('1 ICON "missing.ico"\n2 ICON "here.ico"\n')
        open(os.path.join(source, 'lib', 'here.ico'), 'wb').close()
        with open(os.path.join(build, 'build.ninja'), 'w') as stream:
            stream.write('rule gen\n  command = touch $out\n'
                         'rule C_COMPILER__x\n  command = cc $INCLUDES -c $in -o $out\n'
                         'rule RC_COMPILER__x\n  command = windres $INCLUDES $in $out\n'
                         'build gen/gen.h: gen\n'
                         'build gen/CMakeFiles/genhdr: phony gen/gen.h\n'
                         'build genhdr: phony gen/CMakeFiles/genhdr\n'
                         'build lib/CMakeFiles/a.dir/a.c.obj: C_COMPILER__x %s/lib/a.c\n  INCLUDES = -I%s/gen\n'
                         'build lib/CMakeFiles/b.dir/b.c.obj: C_COMPILER__x %s/lib/b.c || gen/CMakeFiles/genhdr\n  INCLUDES = -I%s/gen\n'
                         'build lib/CMakeFiles/r.dir/r.rc.res: RC_COMPILER__x %s/lib/r.rc\n'
                         'build broken line without separator\n'
                         % (source, build, source, build, source))
        result = analyze(build, source, 'static', 1, False, 10)
        gaps = {key[0]: set(files) for key, files in result['gaps'].items()}
        depfile_result = analyze(build, source, 'depfiles', 1, False, 10) if shutil.which('ninja') else None
        checks = [
            ('depfile mode skips steps without recorded dependencies', depfile_result is None or not depfile_result['gaps']),
            ('static findings do not block', not result['blocking']),
            ('suggestion names the generator target', result['suggestions'].get(('a', 'lib'), ([], False)) == (['genhdr'], True)),
            ('gap without a path to the generator is reported', gaps.get('a') == {os.path.join(build, 'gen', 'gen.h')}),
            ('gap with an order-only path is not reported', 'b' not in gaps),
            ('generator target is named', 'genhdr' in result['owner'].get(os.path.join(build, 'gen', 'gen.h'), ())),
            ('missing resource is reported', any(name.startswith('missing.ico') for _, name in result['missing_resources'])),
            ('untracked resource is reported', any(os.path.join(source, 'lib', 'here.ico') in files for files in result['untracked'].values())),
            ('malformed build line is a warning', any('rule separator' in w for w in result['warnings'])),
        ]
        cycle = Graph.__new__(Graph)
        cycle.producer = {}
        loop_a, loop_b = Edge(), Edge()
        loop_a.outs, loop_a.ins = ['x'], ['y']
        loop_b.outs, loop_b.ins = ['y'], ['x']
        cycle.producer = {'x': loop_a, 'y': loop_b}
        checks.append(('dependency cycle terminates', Reachability(cycle, {'x'}).of_node('x') == 1))
        for name, ok in checks:
            print('%s: %s' % ('ok' if ok else 'FAIL', name))
        return EXIT_CLEAN if all(ok for _, ok in checks) else EXIT_ERROR


def main():
    if sys.version_info < (3, 9):
        print('depcheck: Python 3.9 or later is required', file=sys.stderr)
        return EXIT_ERROR
    parser = argparse.ArgumentParser(description='Check a configured CMake/Ninja tree file by file for dependency gaps: generated headers read without a path to their generator, missing includes and resources, untracked embedded files and undeclared custom command inputs.')
    parser.add_argument('build_dir', nargs='?', help='configured build directory containing build.ninja')
    parser.add_argument('--source-dir', help='source tree (default: parent of the build directory)')
    parser.add_argument('--mode', choices=('compiler', 'depfiles', 'static'), default='compiler',
                        help="compiler: run each step's preprocessor with -M -MG (exact, no build needed); depfiles: use the recorded dependencies of a previous build; static: scan #include lines without evaluating conditions")
    parser.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    parser.add_argument('--timeout', type=int, default=120, help='seconds allowed for one preprocessor run')
    parser.add_argument('--nested', action='store_true', help='also check nested build trees found under the build directory')
    parser.add_argument('--json', action='store_true')
    parser.add_argument('--verbose', action='store_true')
    parser.add_argument('--fail-on-gaps', action='store_true', help='exit with 1 when an exact ordering gap in a default target or a missing include is found')
    parser.add_argument('--all-targets', action='store_true', help='also report missing includes of targets the default build skips')
    parser.add_argument('--self-test', action='store_true', help='check the tool against a synthetic tree and exit')
    args = parser.parse_args()

    if args.self_test:
        return self_test()
    if not args.build_dir:
        parser.error('a build directory is required')
    build_dir = os.path.realpath(args.build_dir)
    source_dir = os.path.realpath(args.source_dir or os.path.dirname(build_dir))
    if not os.path.isfile(os.path.join(build_dir, 'build.ninja')):
        print('depcheck: %s has no build.ninja' % build_dir, file=sys.stderr)
        return EXIT_ERROR
    if not os.path.isdir(source_dir):
        print('depcheck: source directory %s does not exist' % source_dir, file=sys.stderr)
        return EXIT_ERROR
    if args.mode == 'depfiles' and not shutil.which('ninja'):
        print('depcheck: --mode depfiles needs ninja in PATH', file=sys.stderr)
        return EXIT_ERROR
    trees = [build_dir]
    if args.nested:
        for root, dirs, files in os.walk(build_dir):
            dirs[:] = sorted(d for d in dirs if d != 'CMakeFiles')
            if root != build_dir and 'build.ninja' in files and 'CMakeCache.txt' in files:
                trees.append(root)
                dirs[:] = []
    failed = False
    try:
        for tree in trees:
            result = analyze(tree, source_dir, args.mode, args.jobs, not args.json, max(1, args.timeout), args.all_targets)
            report(tree, source_dir, result, args.json, args.verbose)
            failed |= bool(result['blocking']) or bool(result['missing_includes'])
    except DepcheckError as error:
        print('depcheck: %s' % error, file=sys.stderr)
        return EXIT_ERROR
    except KeyboardInterrupt:
        print('depcheck: interrupted', file=sys.stderr)
        return EXIT_ERROR
    return EXIT_GAPS if failed and args.fail_on_gaps else EXIT_CLEAN


if __name__ == '__main__':
    sys.exit(main())
