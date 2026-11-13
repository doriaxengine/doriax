#!/usr/bin/env python3
"""
Parses LuaBridge binding files to generate engine_api_suggestions.h
This is invoked at build time by CMake.

It scans engine/core/script/binding/*.cpp for:
  - beginNamespace / addVariable  → enums
  - beginClass / deriveClass      → classes
  - addConstructor                → constructors
  - addFunction / addStaticFunction → methods
  - addProperty / addStaticProperty → properties
"""

import re
import sys
import os
import glob

# ── Regex patterns ──────────────────────────────────────────────────────────

RE_NAMESPACE_BEGIN = re.compile(r'\.beginNamespace\(\s*"([^"]+)"\s*\)')
RE_NAMESPACE_END = re.compile(r'\.endNamespace\(\)')
RE_ADD_VARIABLE = re.compile(r'\.addVariable\(\s*"([^"]+)"')

RE_BEGIN_CLASS = re.compile(
    r'\.beginClass\s*<\s*([^>]+?)\s*>\s*\(\s*"([^"]+)"\s*\)'
)
RE_DERIVE_CLASS = re.compile(
    r'\.deriveClass\s*<\s*([^,>]+?)\s*,\s*([^>]+?)\s*>\s*\(\s*"([^"]+)"\s*\)'
)
RE_END_CLASS = re.compile(r'\.endClass\(\)')

RE_ADD_CONSTRUCTOR = re.compile(r'\.addConstructor\s*<([^>]+)>')

RE_ADD_FUNCTION = re.compile(r'\.addFunction\(\s*"([^"]+)"')
RE_ADD_STATIC_FUNCTION = re.compile(r'\.addStaticFunction\(\s*"([^"]+)"')

RE_ADD_PROPERTY = re.compile(r'\.addProperty\(\s*"([^"]+)"')
RE_ADD_STATIC_PROPERTY = re.compile(r'\.addStaticProperty\(\s*"([^"]+)"')
# Static property read through an accessor, like &Engine::getDeltatime
RE_STATIC_ACCESSOR = re.compile(r'&[A-Za-z0-9_]+::(?:get|is|has)[A-Z]')

# Property getters: a cast naming the return type, or a plain member pointer
RE_GETTER_CAST = re.compile(r'\((.+?)\([A-Za-z0-9_]+::\*\)')
RE_GETTER_MEMBER = re.compile(r'&([A-Za-z0-9_]+)::([A-Za-z0-9_]+)$')
# Containers LuaBridge pushes as a new table
RE_TABLE_CONTAINER = re.compile(r'(?:vector|array|list|map|unordered_map|set|pair|tuple)\s*<')
# Lambda binding with a trailing return type: [] (const T& self, ...) -> Type {
RE_LAMBDA_SIGNATURE = re.compile(r'\[\]\s*\(([^)]*)\)\s*->\s*([^{]+?)\s*\{')

EXCLUDED_HEADER_METHODS = {
    ('EntityHandle', 'addComponent'),
    ('EntityHandle', 'removeComponent'),
    ('EntityHandle', 'getComponent'),
    ('EntityRegistry', 'addComponent'),
    ('EntityRegistry', 'removeComponent'),
    ('EntityRegistry', 'getComponent'),
    ('EntityRegistry', 'findComponent'),
    ('EntityRegistry', 'findComponentFromIndex'),
    ('EntityRegistry', 'getComponentFromIndex'),
    ('EntityRegistry', 'getComponentId'),
    ('EntityRegistry', 'getComponentArray'),
}


class APISymbol:
    """Represents one symbol in the engine API."""
    __slots__ = ('name', 'kind', 'detail', 'parent', 'getter', 'signature', 'bound', 'shared', 'readonly')

    def __init__(self, name, kind, detail='', parent='', getter='', signature=None, bound=None, shared=False, readonly=False):
        self.name = name
        self.kind = kind
        self.detail = detail
        self.parent = parent
        self.getter = getter  # getter of a writable property
        self.signature = signature  # (params, return) of a lambda binding
        self.bound = bound  # parameter types of the overload Lua gets, None when the binding names none
        self.shared = shared  # a static property, read on the class
        self.readonly = readonly  # bound with a getter and nothing to set it

    def __repr__(self):
        return f'APISymbol({self.name!r}, {self.kind!r})'


def parse_binding_file(filepath):
    """Parse a single LuaBridge binding .cpp file."""
    symbols = []
    with open(filepath, 'r') as f:
        content = f.read()

    # Remove C++ comments
    content = re.sub(r'//.*', '', content)
    content = re.sub(r'/\*.*?\*/', '', content, flags=re.DOTALL)

    # ── Parse enums (namespace-based) ───────────────────────────────────
    ns_pattern = re.compile(
        r'\.beginNamespace\(\s*"([^"]+)"\s*\)(.*?)\.endNamespace\(\)',
        re.DOTALL
    )
    for m in ns_pattern.finditer(content):
        ns_name = m.group(1)
        ns_body = m.group(2)

        # Skip internal FunctionSubscribe namespaces
        if 'FunctionSubscribe' in ns_name:
            continue

        values = RE_ADD_VARIABLE.findall(ns_body)
        if values:
            symbols.append(APISymbol(ns_name, 'Enum', f'enum {ns_name}'))
            for val_name in values:
                symbols.append(APISymbol(
                    val_name, 'EnumMember', f'{ns_name}.{val_name}', ns_name
                ))

    # ── Parse classes ───────────────────────────────────────────────────
    # We process the file line-by-line with state tracking
    lines = content.split('\n')
    current_class = None
    current_lua_name = None
    current_base = None
    current_constructors = []
    pending_constructor = None

    for index, line in enumerate(lines):
        stripped = line.strip()

        if pending_constructor is not None:
            pending_constructor += ' ' + stripped
            if '>' in stripped:
                current_constructors.extend(
                    _parse_constructor_details(pending_constructor, current_lua_name)
                )
                pending_constructor = None
            continue

        # beginClass
        m = RE_BEGIN_CLASS.search(stripped)
        if m:
            current_class = m.group(1).strip()
            current_lua_name = m.group(2)
            current_base = None
            current_constructors = []
            pending_constructor = None
            continue

        # deriveClass
        m = RE_DERIVE_CLASS.search(stripped)
        if m:
            current_class = m.group(1).strip()
            current_base = m.group(2).strip()
            current_lua_name = m.group(3)
            current_constructors = []
            pending_constructor = None
            continue

        # endClass
        if RE_END_CLASS.search(stripped):
            if current_lua_name:
                base_info = f' : {current_base}' if current_base else ''
                detail = f'class {current_lua_name}{base_info}'
                symbols.append(APISymbol(current_lua_name, 'Class', detail))
                for constructor_detail in current_constructors:
                    symbols.append(APISymbol(
                        current_lua_name, 'Constructor',
                        constructor_detail, current_lua_name
                    ))
            current_class = None
            current_lua_name = None
            current_base = None
            current_constructors = []
            pending_constructor = None
            continue

        if not current_lua_name:
            continue

        # Constructor
        if '.addConstructor' in stripped:
            if '>' in stripped:
                current_constructors.extend(
                    _parse_constructor_details(stripped, current_lua_name)
                )
            else:
                pending_constructor = stripped
            continue

        # Static function
        m = RE_ADD_STATIC_FUNCTION.search(stripped)
        if m:
            fname = m.group(1)
            if not fname.startswith('__'):
                symbols.append(APISymbol(
                    fname, 'StaticMethod',
                    f'{current_lua_name}.{fname}()', current_lua_name,
                    bound=_bound_types(_binding_text(lines, index))
                ))
            continue

        # Member function
        m = RE_ADD_FUNCTION.search(stripped)
        if m:
            fname = m.group(1)
            if not fname.startswith('__'):
                symbols.append(APISymbol(
                    fname, 'Method',
                    f'{current_lua_name}:{fname}()', current_lua_name,
                    signature=_lambda_signature(stripped[m.end():]),
                    bound=_bound_types(_binding_text(lines, index))
                ))
            continue

        # Static property
        m = RE_ADD_STATIC_PROPERTY.search(stripped)
        if m:
            pname = m.group(1)
            if not pname.startswith('__'):
                # Check if it's an event callback (lambda pattern)
                if '[]' in stripped or 'lua_State' in stripped:
                    kind = 'Event'
                elif RE_STATIC_ACCESSOR.search(stripped):
                    kind = 'Property'  # Lua-only, C++ calls the accessor
                else:
                    kind = 'Constant'
                symbols.append(APISymbol(
                    pname, kind,
                    f'{current_lua_name}.{pname}', current_lua_name,
                    shared=True,
                    readonly=_getter_only(_call_arguments(_binding_text(lines, index)[m.start():]))
                ))
            continue

        # Member property
        m = RE_ADD_PROPERTY.search(stripped)
        if m:
            pname = m.group(1)
            if not pname.startswith('__'):
                args = _call_arguments(stripped[m.start():])
                symbols.append(APISymbol(
                    pname, 'Property',
                    f'{current_lua_name}.{pname}', current_lua_name,
                    getter=args[1] if len(args) > 2 else '',
                    readonly=_getter_only(_call_arguments(_binding_text(lines, index)[m.start():]))
                ))
            continue

    return symbols


def kind_to_suggestion_kind(kind):
    """Map our internal kind to SuggestionKind enum."""
    mapping = {
        'Class': 'Class',
        'Constructor': 'Class',
        'Enum': 'Enum',
        'EnumMember': 'EnumMember',
        'Method': 'Method',
        'CppMethod': 'CppMethod',  # C++ only; kept distinct so Lua consumers can drop it
        'StaticMethod': 'Function',
        'Property': 'Property',
        'Constant': 'Constant',
        'Event': 'Property',
    }
    return mapping.get(kind, 'Variable')


def _extract_class_body(text, start):
    """Extract a brace-delimited class body starting from the '{' at position start."""
    depth = 0
    i = start
    while i < len(text):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[start + 1:i]
        i += 1
    return ''


RE_ACCESS_SPECIFIER = re.compile(r'(public|protected|private)\s*:(?!:)')


def _public_declarations(body, default_public):
    """Every public section of a class body at its own depth; nested bodies are emptied to '{}'."""
    out = []
    public = default_public
    depth = 0
    i = 0
    while i < len(body):
        c = body[i]
        if c == '{':
            if depth == 0 and public:
                out.append(c)
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0 and public:
                out.append(c)
        elif depth == 0:
            m = RE_ACCESS_SPECIFIER.match(body, i) if c == 'p' else None
            if m and (i == 0 or not (body[i - 1].isalnum() or body[i - 1] == '_')):
                public = m.group(1) == 'public'
                i = m.end()
                continue
            if public:
                out.append(c)
        i += 1
    return ''.join(out)


def _simplify_param(param):
    """Simplify a C++ parameter to 'Type name' form, stripping const/ref/ptr qualifiers."""
    p = param.strip()
    if not p:
        return ''
    # Remove 'const' prefix
    p = re.sub(r'\bconst\s+', '', p)
    # Remove trailing const
    p = re.sub(r'\s+const$', '', p)
    # Remove std:: prefix
    p = re.sub(r'\bstd::', '', p)
    # Remove reference/pointer from type: 'string&' -> 'string'
    p = re.sub(r'\s*[&*]+\s*', ' ', p)
    # Collapse whitespace
    p = re.sub(r'\s+', ' ', p).strip()
    return p


def _simplify_return_type(ret):
    """Reduce a C++ return type to a bare type name, or '' to omit it.

    Returns '' for void and for anything that doesn't look like a clean type,
    so a misparsed signature never injects garbage into a suggestion detail.
    """
    if not ret:
        return ''
    t = re.sub(r'\b(?:virtual|inline|static|constexpr|noexcept|const)\b', ' ', ret)
    t = re.sub(r'\bstd::', '', t)
    t = re.sub(r'[&*]+', ' ', t)
    t = re.sub(r'\s+', ' ', t).strip()
    if not t or t == 'void':
        return ''
    # Reject control-flow/statement keywords that leak in when the method regex
    # accidentally matches a call inside an inline body (e.g. 'return Foo(...)').
    if t.split()[0] in _RETURN_TYPE_REJECT:
        return ''
    # Only accept identifier-like types (incl. templates/scopes), never stray tokens.
    if not re.match(r'^[A-Za-z_][A-Za-z0-9_:<>, ]*$', t):
        return ''
    return t


_RETURN_TYPE_REJECT = {
    'return', 'if', 'else', 'for', 'while', 'switch', 'case', 'do',
    'break', 'continue', 'goto', 'using', 'typedef', 'friend', 'template',
}


def _getter_return_type(getter, cpp_methods):
    """C++ return type of a property getter, or '' when unknown."""
    m = RE_GETTER_CAST.match(getter)
    if m:
        return m.group(1)
    m = RE_GETTER_MEMBER.match(getter)
    if not m:
        return ''
    # Data members are not in cpp_methods: Lua gets a reference to them
    overloads = cpp_methods.get(m.group(1), {}).get(m.group(2), [])
    return next((ret for params, ret in overloads if not params), '')


def _copied_type(ret, class_names):
    """Type Lua gets as a copy from a getter returning ret, or ''."""
    t = _simplify_return_type(ret)
    # Containers always become a new table, bound classes only when returned by value
    if RE_TABLE_CONTAINER.match(t) or (t in class_names and not re.search(r'[&*]', ret)):
        return t
    return ''


def _lambda_signature(text):
    """(params, return) of a lambda binding, which wins over the C++ header's."""
    m = RE_LAMBDA_SIGNATURE.search(text)
    if not m:
        return None
    # Lua passes neither self nor the lua_State
    params = [p for p in m.group(1).split(',')[1:] if 'lua_State' not in p]
    return _simplify_params(','.join(params)), m.group(2)


RE_GETTER_FUNCTION = re.compile(r'&\s*[A-Za-z0-9_:]+::(?:get|is|has|can)[A-Z]|\[[^\]]*\]\s*\(|\(\s*\*\s*\)|::\s*\*\s*\)')


def _getter_only(args):
    """Whether a property binding gives Lua nothing to set: one function after the name.

    A pointer to a data member, the other one-argument form, is written through.
    """
    return len(args) == 2 and bool(RE_GETTER_FUNCTION.search(args[1]))


def _binding_text(lines, index):
    """An add call with the lines it runs over, up to the next call of the chain."""
    text = lines[index].strip()
    for following in lines[index + 1:index + 12]:
        s = following.strip()
        if s.startswith('.') or s.startswith('//') or not s:
            break
        text += ' ' + s
        if ';' in s:
            break
    return text


RE_BOUND_CAST = re.compile(r'\(\s*[\w:<>\s&*]+\(\s*(?:\w+::)?\*\s*\)\s*\(([^()]*)\)')


def _split_types(text):
    """A parameter list split at the commas outside templates."""
    parts, depth, current = [], 0, ''
    for c in text:
        if c == '<':
            depth += 1
        elif c == '>':
            depth -= 1
        if c == ',' and depth == 0:
            parts.append(current)
            current = ''
        else:
            current += c
    parts.append(current)
    return [_simplify_param(p) for p in parts if p.strip() and p.strip() != 'void']


def _bound_types(text):
    """Parameter types of the overload a binding gives Lua, or None when it names none.

    Of several, the one without parameters is preferred, then the one that
    takes a single value (a vector rather than its numbers), else the first.
    """
    found = []
    marker = 'luabridge::overload<'
    start = text.find(marker)
    while start >= 0:
        # templates nest, as overload<std::vector<int>>
        begin = start + len(marker)
        depth, end = 1, begin
        while end < len(text) and depth:
            depth += 1 if text[end] == '<' else (-1 if text[end] == '>' else 0)
            end += 1
        if depth:
            break
        found.append(_split_types(text[begin:end - 1]))
        start = text.find(marker, end)
    if not found:
        m = RE_BOUND_CAST.search(text)
        return _split_types(m.group(1)) if m else None
    for count in (0, 1):
        for types in found:
            if len(types) == count:
                return types
    return found[0]


def _param_type(param):
    """The type of a simplified 'Type name' parameter."""
    param = param.split('=')[0].strip()
    parts = param.rsplit(' ', 1)
    return parts[0] if len(parts) == 2 else param


def _bound_overload(overloads, types):
    """The header overload Lua calls, with its parameter names; made of the types alone when none matches."""
    for params, ret in overloads:
        if [_param_type(p) for p in _split_types(params)] == types:
            return params, ret
    returns = overloads[0][1] if overloads else ''
    return ', '.join(f'{t} value{i + 1}' for i, t in enumerate(types)), returns


def _simplify_params(params_str):
    """Simplify a full parameter list string."""
    if not params_str.strip() or params_str.strip() == 'void':
        return ''
    params = params_str.split(',')
    simplified = []
    for p in params:
        s = _simplify_param(p)
        if s:
            simplified.append(s)
    return ', '.join(simplified)


def _parse_constructor_details(text, class_name):
    """Extract Lua constructor overload signatures from an addConstructor call."""
    if not class_name:
        return []

    m = re.search(r'\.addConstructor\s*<(.+?)>\s*\(', text)
    if not m:
        return [f'{class_name}(...)']

    raw = m.group(1)
    signature_pattern = re.compile(r'void\s*(?:\(\s*\*\s*\))?\s*\(([^()]*)\)')
    details = []
    seen = set()
    for params in signature_pattern.findall(raw):
        simplified = _simplify_params(params)
        detail = f'{class_name}({simplified})'
        if detail not in seen:
            seen.add(detail)
            details.append(detail)

    return details or [f'{class_name}()']


def _call_arguments(text):
    """Top-level arguments of the first call in text."""
    args = []
    arg = ''
    depth = 0
    for c in text:
        if c in '([{':
            depth += 1
            if depth == 1:
                continue
        elif c in ')]}':
            depth -= 1
            if depth == 0:
                break
        elif c == ',' and depth == 1:
            args.append(arg.strip())
            arg = ''
            continue
        if depth:
            arg += c
    args.append(arg.strip())
    return args


def parse_cpp_headers(base_dir):
    """Parse C++ headers to extract public method declarations with parameter info."""
    # class_methods[class_name] = { method_name: [(params_str, return_type), ...] }
    class_methods = {}
    sig_pattern = re.compile(
        r'(?:virtual\s+|inline\s+|static\s+)*'
        r'([A-Za-z0-9_:<>&*\s]+)\s+'
        r'([A-Za-z0-9_]+)\s*\(([^)]*)\)\s*(?:const)?\s*(?:override|final)?\s*[;{]'
    )
    class_start = re.compile(
        r'(?:class|struct)\s+(?:[A-Z0-9_]+\s+)?([A-Za-z0-9_]+)\s*(?::[^{]+)?\{'
    )

    for root, dirs, files in os.walk(base_dir):
        for f in files:
            if not f.endswith('.h'):
                continue
            path = os.path.join(root, f)
            with open(path, 'r', encoding='utf-8', errors='ignore') as fh:
                text = fh.read()

            # Strip comments
            text = re.sub(r'//.*', '', text)
            text = re.sub(r'/\*.*?\*/', '', text, flags=re.DOTALL)

            for cm in class_start.finditer(text):
                class_name = cm.group(1)
                brace_pos = cm.end() - 1  # position of '{'
                body = _extract_class_body(text, brace_pos)
                if not body:
                    continue

                # A class can reopen 'public:' after a private block (Engine does)
                public_body = _public_declarations(body, cm.group(0).startswith('struct'))
                if not public_body.strip():
                    continue

                if class_name not in class_methods:
                    class_methods[class_name] = {}

                for m in sig_pattern.finditer(public_body):
                    ret_type = m.group(1).strip()
                    method_name = m.group(2)
                    params_raw = m.group(3).strip()
                    if method_name == class_name or method_name.startswith('~'):
                        continue
                    # "operator bool()" is a conversion, not a method named "bool"
                    if re.search(r'\boperator$', ret_type):
                        continue
                    if (class_name, method_name) in EXCLUDED_HEADER_METHODS:
                        continue
                    params = _simplify_params(params_raw)
                    if method_name not in class_methods[class_name]:
                        class_methods[class_name][method_name] = []
                    class_methods[class_name][method_name].append((params, ret_type))

    return class_methods

def generate_header(symbols, output_path):
    """Generate engine_api_suggestions.h from parsed symbols."""
    # Deduplicate: keep first occurrence per (name, kind, parent) tuple
    seen = set()
    unique = []
    for s in symbols:
        key = (s.name, s.kind, s.parent, s.detail if s.kind == 'Constructor' else '')
        if key not in seen:
            seen.add(key)
            unique.append(s)

    # Separate into categories for organized output
    enums = [s for s in unique if s.kind == 'Enum']
    enum_members = [s for s in unique if s.kind == 'EnumMember']
    classes = [s for s in unique if s.kind == 'Class']
    constructors = [s for s in unique if s.kind == 'Constructor']
    methods = [s for s in unique if s.kind == 'Method']
    cpp_methods_only = [s for s in unique if s.kind == 'CppMethod']
    static_methods = [s for s in unique if s.kind == 'StaticMethod']
    properties = [s for s in unique if s.kind == 'Property']
    constants = [s for s in unique if s.kind == 'Constant']
    events = [s for s in unique if s.kind == 'Event']

    def escape(s):
        return s.replace('\\', '\\\\').replace('"', '\\"')

    lines = []
    lines.append('// Auto-generated by generate_api_suggestions.py — DO NOT EDIT')
    lines.append('// Parsed from engine/core/script/binding/*.cpp')
    lines.append('#pragma once')
    lines.append('')
    lines.append('#include <string>')
    lines.append('#include <vector>')
    lines.append('#include <unordered_set>')
    lines.append('')
    lines.append('namespace doriax::editor {')
    lines.append('')

    # ── Engine type names (for syntax highlighting in both Lua and C++) ──
    lines.append('inline std::unordered_set<std::string> getEngineTypeNames() {')
    lines.append('    return {')
    class_names = sorted(set(s.name for s in classes))
    for name in class_names:
        lines.append(f'        "{escape(name)}",')
    lines.append('    };')
    lines.append('}')
    lines.append('')

    # ── Engine enum names ────────────────────────────────────────────────
    lines.append('inline std::unordered_set<std::string> getEngineEnumNames() {')
    lines.append('    return {')
    enum_names = sorted(set(s.name for s in enums))
    for name in enum_names:
        lines.append(f'        "{escape(name)}",')
    lines.append('    };')
    lines.append('}')
    lines.append('')

    # ── Engine builtin function names (static classes used as modules) ──
    lines.append('inline std::unordered_set<std::string> getEngineBuiltinNames() {')
    lines.append('    return {')
    # Classes that are used as static modules (all static functions, no constructor)
    static_classes = set()
    for s in static_methods:
        static_classes.add(s.parent)
    # Also add enum names as builtins for Lua namespace access
    all_builtins = sorted(static_classes | set(enum_names))
    for name in all_builtins:
        lines.append(f'        "{escape(name)}",')
    lines.append('    };')
    lines.append('}')
    lines.append('')

    # ── Full suggestion symbols ──────────────────────────────────────────
    lines.append('struct EngineAPISymbol {')
    lines.append('    const char* name;')
    lines.append('    const char* kind;   // maps to SuggestionKind')
    lines.append('    const char* detail;')
    lines.append('    const char* parent; // owning class/enum')
    lines.append('};')
    lines.append('')
    # ── Static properties, as "Class.name" ───────────────────────────────
    lines.append('inline std::unordered_set<std::string> getEngineStaticProperties() {')
    lines.append('    return {')
    for name in sorted(set(f'{s.parent}.{s.name}' for s in unique if s.shared and s.kind == 'Property')):
        lines.append(f'        "{escape(name)}",')
    lines.append('    };')
    lines.append('}')
    lines.append('')

    # ── Properties Lua can only read, as "Class.name" ────────────────────
    lines.append('inline std::unordered_set<std::string> getEngineReadOnlyProperties() {')
    lines.append('    return {')
    for name in sorted(set(f'{s.parent}.{s.name}' for s in unique if s.readonly)):
        lines.append(f'        "{escape(name)}",')
    lines.append('    };')
    lines.append('}')
    lines.append('')

    lines.append('inline const std::vector<EngineAPISymbol>& getEngineAPISymbols() {')
    lines.append('    static const std::vector<EngineAPISymbol> symbols = {')

    all_symbols = enums + enum_members + classes + constructors + methods + static_methods + \
                  properties + constants + events + cpp_methods_only
    for s in all_symbols:
        sk = kind_to_suggestion_kind(s.kind)
        lines.append(
            f'        {{"{escape(s.name)}", "{sk}", '
            f'"{escape(s.detail)}", "{escape(s.parent)}"}},')

    lines.append('    };')
    lines.append('    return symbols;')
    lines.append('}')
    lines.append('')
    lines.append('} // namespace doriax::editor')
    lines.append('')

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, 'w') as f:
        f.write('\n'.join(lines))

    print(f'[generate_api_suggestions] Generated {output_path}')
    print(f'  Classes: {len(classes)}, Methods: {len(methods)}, '
          f'CppMethods: {len(cpp_methods_only)}, '
          f'StaticMethods: {len(static_methods)}, Properties: {len(properties)}, '
          f'Constants: {len(constants)}, Events: {len(events)}, '
          f'Enums: {len(enums)}, EnumMembers: {len(enum_members)}')


def main():
    if len(sys.argv) < 3:
        print(f'Usage: {sys.argv[0]} <binding_dir> <output_header>')
        sys.exit(1)

    if len(sys.argv) < 3:
        print(f'Usage: {sys.argv[0]} <binding_dir> <output_header> [engine_core_dir]')
        sys.exit(1)

    binding_dir = sys.argv[1]
    output_path = sys.argv[2]
    engine_core_dir = sys.argv[3] if len(sys.argv) > 3 else os.path.abspath(os.path.join(binding_dir, '..', '..'))

    binding_files = sorted(glob.glob(os.path.join(binding_dir, '*.cpp')))
    if not binding_files:
        print(f'WARNING: No binding files found in {binding_dir}')
        sys.exit(1)

    all_symbols = []
    for filepath in binding_files:
        syms = parse_binding_file(filepath)
        all_symbols.extend(syms)
        print(f'  Parsed {os.path.basename(filepath)}: {len(syms)} symbols')

    cpp_methods = parse_cpp_headers(engine_core_dir)

    def _format_detail(parent, name, sep, overload):
        """Build a 'Parent<sep>name(params) -> Return' detail from a (params, return) pair."""
        params, ret_type = overload
        detail = f'{parent}{sep}{name}({params})'
        returns = _simplify_return_type(ret_type)
        if returns:
            detail += f' -> {returns}'
        return detail

    # Build a lookup of all method signatures: (class, method) -> (params, return)
    cpp_signatures = {}  # (class_name, method_name) -> shortest (params, return) overload
    for cls, methods_dict in cpp_methods.items():
        for method_name, overloads in methods_dict.items():
            # Pick the shortest overload as primary detail (most common / simplest)
            shortest = min(overloads, key=lambda o: len(o[0])) if overloads else ('', '')
            cpp_signatures[(cls, method_name)] = shortest

    # Update existing LuaBridge method symbols with param + return info from C++ headers
    for s in all_symbols:
        if s.kind in ('Method', 'StaticMethod') and s.parent:
            sig = s.signature or cpp_signatures.get((s.parent, s.name))
            # the header's shortest overload is not always the one bound
            if s.signature is None and s.bound is not None:
                sig = _bound_overload(cpp_methods.get(s.parent, {}).get(s.name, []), s.bound)
            if sig is not None:
                sep = '.' if s.kind == 'StaticMethod' else ':'
                s.detail = _format_detail(s.parent, s.name, sep, sig)

    lua_classes = {s.name for s in all_symbols if s.kind == 'Class'}

    # Tag properties whose getter gives Lua a copy (obj.position.y = 1 changes nothing)
    for s in all_symbols:
        if s.kind == 'Property' and s.getter:
            copied = _copied_type(_getter_return_type(s.getter, cpp_methods), lua_classes)
            if copied:
                s.detail += f' -> {copied} (copy)'

    # C++ methods a bound class does NOT expose to Lua: 'CppMethod' + Class::method,
    # so nothing offers them as Lua calls that would resolve to nil.
    lua_bound_methods = {
        (s.name, s.parent) for s in all_symbols
        if s.kind in ('Method', 'StaticMethod')
    }
    cpp_symbols = []
    for cls in lua_classes:
        methods_dict = cpp_methods.get(cls, {})
        for method_name, overloads in methods_dict.items():
            if (method_name, cls) not in lua_bound_methods:
                shortest = min(overloads, key=lambda o: len(o[0])) if overloads else ('', '')
                cpp_symbols.append(APISymbol(
                    method_name, 'CppMethod',
                    _format_detail(cls, method_name, '::', shortest), cls
                ))
    all_symbols.extend(cpp_symbols)

    generate_header(all_symbols, output_path)


if __name__ == '__main__':
    main()
