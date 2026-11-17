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
import json
import shutil
import textwrap

# ── Regex patterns ──────────────────────────────────────────────────────────

# Metamethods Lua stubs type, by their LuaLS name
LUA_OPERATORS = {'__add': 'add', '__sub': 'sub', '__mul': 'mul', '__div': 'div', '__unm': 'unm'}
RE_OPERATOR = re.compile(r'([A-Za-z0-9_:<>]+)\s*[&*]?\s+operator\s*([+\-*/])\s*\(([^)]*)\)')

RE_NAMESPACE_BEGIN = re.compile(r'\.beginNamespace\(\s*"([^"]+)"\s*\)')
RE_NAMESPACE_END = re.compile(r'\.endNamespace\(\)')
RE_ADD_VARIABLE = re.compile(r'\.addVariable\(\s*"([^"]+)"')
RE_ADD_VARIABLE_VALUE = re.compile(r'\.addVariable\(\s*"([^"]+)"\s*,\s*([A-Za-z0-9_:]+)\s*\)')

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
    __slots__ = ('name', 'kind', 'detail', 'parent', 'getter', 'signature', 'bound', 'shared', 'readonly', 'accessor', 'boolean',
                 'mutator', 'cpp_read', 'cpp_write', 'callee', 'cpp_result', 'overloads', 'cpp_class', 'base', 'lua_signature')

    def __init__(self, name, kind, detail='', parent='', getter='', signature=None, bound=None, shared=False, readonly=False, accessor=''):
        self.name = name
        self.kind = kind
        self.detail = detail
        self.parent = parent
        self.getter = getter  # getter of a writable property
        self.signature = signature  # (params, return) of a lambda binding
        self.bound = bound  # parameter types of the overload Lua gets, None when the binding names none
        self.shared = shared  # a static property, read on the class
        self.readonly = readonly  # bound with a getter and nothing to set it
        self.accessor = accessor  # what a property is read through, whatever else it is bound with
        self.boolean = False  # a property that holds true or false
        self.mutator = ''  # what a property is set through
        self.cpp_read = ''  # the symbol in C++: a member, a call or a value
        self.cpp_write = ''  # the setter of a property, '=' for a data member
        self.callee = None  # (class, member) a function binding calls
        self.cpp_result = ''  # what the C++ read gives: '*' a pointer, '&' a reference, '' a value
        self.overloads = []  # parameter types of every overload Lua gets
        self.cpp_class = ''  # of a class: its C++ name and base
        self.base = ''
        self.lua_signature = None  # (params, return) the Lua stubs show

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
        cpp_values = dict(RE_ADD_VARIABLE_VALUE.findall(ns_body))
        if values:
            symbols.append(APISymbol(ns_name, 'Enum', f'enum {ns_name}'))
            for val_name in values:
                member = APISymbol(
                    val_name, 'EnumMember', f'{ns_name}.{val_name}', ns_name
                )
                member.cpp_read = cpp_values.get(val_name, '')
                symbols.append(member)

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
                cls = APISymbol(current_lua_name, 'Class', detail)
                cls.cpp_class = current_class
                cls.base = current_base or ''
                symbols.append(cls)
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
                static = APISymbol(
                    fname, 'StaticMethod',
                    f'{current_lua_name}.{fname}()', current_lua_name,
                    bound=_bound_types(_binding_text(lines, index))
                )
                static.overloads = _bound_type_lists(_binding_text(lines, index))
                callee = _cpp_callee(_binding_text(lines, index)[m.end():])
                if callee:
                    static.cpp_read = f'{callee[0]}::{callee[1]}'
                    static.callee = callee
                symbols.append(static)
            continue

        # Member function
        m = RE_ADD_FUNCTION.search(stripped)
        if m:
            fname = m.group(1)
            if fname in LUA_OPERATORS:
                operator = APISymbol(fname, 'Operator', '', current_lua_name)
                text = _binding_text(lines, index)
                # vectorMul takes a number or a vector
                operator.overloads = [['float'], [current_class]] if 'vectorMul<' in text else _bound_type_lists(text)
                symbols.append(operator)
            if not fname.startswith('__'):
                method = APISymbol(
                    fname, 'Method',
                    f'{current_lua_name}:{fname}()', current_lua_name,
                    getter=_bound_member(stripped[m.end():]),
                    signature=_lambda_signature(stripped[m.end():]),
                    bound=_bound_types(_binding_text(lines, index))
                )
                method.overloads = _bound_type_lists(_binding_text(lines, index))
                callee = _cpp_callee(_binding_text(lines, index)[m.end():])
                if callee:
                    method.cpp_read = callee[1]
                    method.callee = callee
                symbols.append(method)
            continue

        # Static property
        m = RE_ADD_STATIC_PROPERTY.search(stripped)
        if m:
            pname = m.group(1)
            if not pname.startswith('__'):
                args = _call_arguments(_binding_text(lines, index)[m.start():])
                # an event takes the Lua function through a lua_State setter
                if len(args) > 2 and 'lua_State' in args[2]:
                    kind = 'Event'
                elif '[]' in stripped or RE_STATIC_ACCESSOR.search(stripped):
                    kind = 'Property'  # Lua-only, read through an accessor or a lambda
                else:
                    kind = 'Constant'
                shared = APISymbol(
                    pname, kind,
                    f'{current_lua_name}.{pname}', current_lua_name,
                    shared=True,
                    readonly=_getter_only(args),
                    accessor=_accessor(args)
                )
                shared.mutator = args[2].strip() if len(args) > 2 else ''
                symbols.append(shared)
            continue

        # Member property
        m = RE_ADD_PROPERTY.search(stripped)
        if m:
            pname = m.group(1)
            if not pname.startswith('__'):
                args = _call_arguments(stripped[m.start():])
                bound_args = _call_arguments(_binding_text(lines, index)[m.start():])
                member = APISymbol(
                    pname, 'Property',
                    f'{current_lua_name}.{pname}', current_lua_name,
                    getter=args[1] if len(args) > 2 else '',
                    readonly=_getter_only(bound_args),
                    accessor=_accessor(bound_args)
                )
                member.mutator = bound_args[2].strip() if len(bound_args) > 2 else ''
                symbols.append(member)
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


def _accessor(args):
    """What a property binding reads through: the argument after the name."""
    return args[1].strip() if len(args) > 1 else ''


def _is_bool_property(accessor, cpp_methods, bool_members):
    """Whether a property read through accessor holds a bool."""
    if _simplify_return_type(_getter_return_type(accessor, cpp_methods)) == 'bool':
        return True
    m = RE_GETTER_MEMBER.match(accessor)
    return bool(m) and m.group(2) in bool_members.get(m.group(1), set())


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
        # a lambda returning self->getSystem<UISystem>()
        m = re.search(r'\[\]\s*\([^)]*\)\s*\{\s*return\s+self->get[A-Za-z]*<([A-Za-z0-9_:]+)>\(\)', text)
        return ('', m.group(1) + '*') if m else None
    # Lua passes neither self nor the lua_State
    params = [p for p in m.group(1).split(',')[1:] if 'lua_State' not in p]
    return _simplify_params(','.join(params)), m.group(2)


RE_GETTER_FUNCTION = re.compile(r'&\s*[A-Za-z0-9_:]+::(?:get|is|has|can)[A-Z]|\[[^\]]*\]\s*\(|\(\s*\*\s*\)|::\s*\*\s*\)')


def _getter_only(args):
    """Whether a property binding gives Lua nothing to set: one function after the name.

    A pointer to a data member, the other one-argument form, is written through.
    """
    return len(args) == 2 and bool(RE_GETTER_FUNCTION.search(args[1]))


RE_BOUND_MEMBER = re.compile(r'&\s*[A-Za-z0-9_]+::([A-Za-z0-9_]+)')


def _bound_member(text):
    """The C++ method a binding names first, which the Lua name may differ from."""
    m = RE_BOUND_MEMBER.search(text)
    return m.group(1) if m else ''


RE_MEMBER_POINTER = re.compile(r'&\s*([A-Za-z0-9_]+)::([A-Za-z0-9_]+)\s*$')
RE_CALLEE = re.compile(r'&\s*([A-Za-z0-9_]+)::([A-Za-z0-9_]+(?:<[A-Za-z0-9_:]+>)?)\s*[,)]')
RE_CONSTANT_COPY = re.compile(r'constantCopy<[^,]+,\s*([A-Za-z0-9_:]+)\s*>')
RE_VALUE_LAMBDA = re.compile(r'^\[\]\s*\(\s*\)\s*(?:->\s*[\w:<> ]+?)?\s*\{\s*return\s+([A-Za-z0-9_:]+)\s*;\s*\}$')
RE_ACCESSOR_NAME = re.compile(r'^(?:get|is|has|can|set)[A-Z]')


def _member_pointer(arg):
    """(class, member) of an &C::m argument, cast or not; None for anything else."""
    m = RE_MEMBER_POINTER.search(arg.strip())
    return (m.group(1), m.group(2)) if m and '[' not in arg else None


def _cpp_callee(text):
    """(class, member) a function binding calls, None when a lambda stands first."""
    m = RE_CALLEE.search(text)
    lam = text.find('[')
    if not m or (0 <= lam < m.start()) or m.group(2) == 'operator':
        return None
    return m.group(1), m.group(2)


def _is_cpp_method(cls, name, cpp_methods, method_names, bool_members):
    """Whether C::name is a method rather than a data member."""
    if name in cpp_methods.get(cls, {}):
        return True
    if name in bool_members.get(cls, set()):
        return False
    return name in method_names and bool(RE_ACCESSOR_NAME.match(name))


def _cpp_property(symbol, cpp_methods, method_names, bool_members):
    """(read, write) of a property in C++: a getter call or a member, and its setter or '='."""
    read = write = ''
    accessor = symbol.accessor
    getter = _member_pointer(accessor)
    scope = (lambda c: c + '::') if symbol.shared else (lambda c: '')
    if getter:
        method = _is_cpp_method(getter[0], getter[1], cpp_methods, method_names, bool_members)
        read = scope(getter[0]) + getter[1] + ('()' if method else '')
        if not method and not symbol.mutator and not symbol.shared:
            write = '='
    elif symbol.shared:
        # the template's comma splits it in two
        m = RE_CONSTANT_COPY.search(accessor + ', ' + symbol.mutator) or RE_VALUE_LAMBDA.match(accessor)
        return (m.group(1) if m else ''), ''
    setter = _member_pointer(symbol.mutator) if symbol.mutator else None
    if read and setter:
        method = _is_cpp_method(setter[0], setter[1], cpp_methods, method_names, bool_members)
        write = scope(setter[0]) + setter[1] if method else '='
    return read, write


def _result_kind(ret):
    """'*' for a pointer return type, '&' for a reference C++ can change through, '' for a value."""
    if '*' in ret:
        return '*'
    if '&' in ret and not re.search(r'\bconst\b', ret):
        return '&'
    return ''


def _callee_return(symbol, cpp_methods):
    """C++ return type of the member a function binding calls, '' when unknown."""
    cls, member = symbol.callee
    # the header list leaves it out; it gives the component itself
    if member.startswith('getComponent<'):
        return 'T&'
    overloads = cpp_methods.get(cls, {}).get(member) or cpp_methods.get(symbol.parent, {}).get(member, [])
    if not overloads:
        return ''
    if symbol.bound is not None:
        return _bound_overload(overloads, symbol.bound)[1]
    return min(overloads, key=lambda o: len(o[0]))[1]


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


def _split_types_raw(text):
    """Text split at the commas outside templates and parentheses."""
    parts, depth, current = [], 0, ''
    for c in text:
        if c in '<(':
            depth += 1
        elif c in '>)':
            depth -= 1
        if c == ',' and depth == 0:
            parts.append(current)
            current = ''
        else:
            current += c
    parts.append(current)
    return parts


def _split_types(text):
    """A parameter list split at the commas outside templates."""
    return [_simplify_param(p) for p in _split_types_raw(text) if p.strip() and p.strip() != 'void']


def _bound_type_lists(text):
    """Parameter types of every overload a binding gives Lua, empty when it names none."""
    found = []
    text = text.replace('luabridge::constOverload<', 'luabridge::overload<')
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
        return [_split_types(m.group(1))] if m else []
    return found


def _bound_types(text):
    """Parameter types of the overload a binding gives Lua, or None when it names none.

    Of several, the one without parameters is preferred, then the one that
    takes a single value (a vector rather than its numbers), else the first.
    """
    found = _bound_type_lists(text)
    if not found:
        return None
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


RE_ENUM_START = re.compile(r'\benum\s+(?:class\s+)?([A-Za-z0-9_]+)\s*(?::[^{;]+)?\{')

# type_headers[name] = header of a class or enum, from the engine core
type_headers = {}
# pointer_members[class] = its public data members that are pointers
pointer_members = {}
# member_types[class][member] = C++ type of a public data member
member_types = {}
# constructors[class] = parameter lists of its public constructors
constructors = {}
# enum_values[enum][member] = its value, when the header gives one
enum_values = {}
# operator_returns[class][(operator, operand type)] = what it gives
operator_returns = {}


def _enum_number(expr, values):
    """An enum initializer of numbers, earlier members, << and |; None for anything else."""
    result = 0
    for term in expr.split('|'):
        operands = []
        for operand in term.split('<<'):
            operand = operand.strip()
            if operand in values:
                operands.append(values[operand])
                continue
            try:
                operands.append(int(operand.rstrip('uUlL'), 0))
            except ValueError:
                return None
        if None in operands or len(operands) > 2:
            return None
        result |= operands[0] << operands[1] if len(operands) == 2 else operands[0]
    return result


def _enum_values(body):
    """Value of each member of an enum body, None where it can't be read."""
    values = {}
    current = -1
    for item in body.split(','):
        name, _, expr = item.partition('=')
        name = name.strip()
        if not re.match(r'^[A-Za-z_][A-Za-z0-9_]*$', name):
            continue
        if expr.strip():
            current = _enum_number(expr, values)
        elif current is not None:
            current += 1
        values[name] = current
    return values


def _data_members(body):
    """(name, type) of the data members a public class body declares."""
    members = []
    body = re.sub(r'^\s*#.*$', '', body, flags=re.M)
    # statements end at ; or at the braces of an inline body
    for statement in re.split(r'[;{}]', body):
        statement = re.sub(r'\s+', ' ', statement).strip()
        if not statement or re.match(r'^(?:typedef|using|friend|enum|class|struct|template|static_assert|operator)\b', statement):
            continue
        # a default value, outside template brackets
        depth = 0
        for i, c in enumerate(statement):
            depth += 1 if c == '<' else (-1 if c == '>' else 0)
            if c == '=' and depth == 0:
                statement = statement[:i].strip()
                break
        if re.sub(r'<[^<>]*(?:<[^<>]*>[^<>]*)*>', '', statement).find('(') >= 0:
            continue
        statement = re.sub(r'^(?:(?:static|inline|mutable|constexpr|const|volatile)\s+)+', '', statement)
        # float x, y declares both
        first, *others = _split_types_raw(statement)
        m = re.match(r'^(.+?[\s*&>])([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[^\]]*\])?$', first.strip())
        if m:
            members.append((m.group(2), m.group(1).strip()))
            for other in others:
                if re.match(r'^[A-Za-z_][A-Za-z0-9_]*$', other.strip()):
                    members.append((other.strip(), m.group(1).strip()))
    return members


def parse_cpp_headers(base_dir):
    """Parse C++ headers to extract public method declarations with parameter info."""
    # class_methods[class_name] = { method_name: [(params_str, return_type), ...] }
    class_methods = {}
    # bool_members[class_name] = { member_name, ... }
    bool_members = {}
    bool_member = re.compile(r'(?<![A-Za-z0-9_])bool\s+([A-Za-z0-9_]+)\s*(?:=[^;(]*)?;')
    pointer_member = re.compile(r'(?<![A-Za-z0-9_:])[A-Za-z_][A-Za-z0-9_:<>]*\s*\*\s*([A-Za-z0-9_]+)\s*(?:=[^;{]*)?;')
    sig_pattern = re.compile(
        r'(?:virtual\s+|inline\s+|static\s+)*'
        r'([A-Za-z0-9_:<>&*\s]+)\s+'
        r'([A-Za-z0-9_]+)\s*\(([^)]*)\)\s*(?:const)?\s*(?:override|final)?\s*(?:=\s*0\s*)?[;{]'
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

            relative = os.path.relpath(path, base_dir).replace(os.sep, '/')
            for em in RE_ENUM_START.finditer(text):
                type_headers.setdefault(em.group(1), relative)
                enum_values.setdefault(em.group(1), _enum_values(text[em.end():text.find('}', em.end())]))

            for cm in class_start.finditer(text):
                class_name = cm.group(1)
                type_headers.setdefault(class_name, relative)
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
                bool_members.setdefault(class_name, set()).update(bool_member.findall(public_body))
                pointer_members.setdefault(class_name, set()).update(pointer_member.findall(public_body))

                for name, cpp_type in _data_members(public_body):
                    member_types.setdefault(class_name, {}).setdefault(name, cpp_type)
                for m in RE_OPERATOR.finditer(public_body):
                    operand = _param_type(_simplify_params(m.group(3))) if m.group(3).strip() else ''
                    operator_returns.setdefault(class_name, {}).setdefault((m.group(2), operand), _simplify_return_type(m.group(1)))

                for m in sig_pattern.finditer(public_body):
                    ret_type = m.group(1).strip()
                    method_name = m.group(2)
                    params_raw = m.group(3).strip()
                    if method_name == class_name:
                        constructors.setdefault(class_name, []).append(_simplify_params(params_raw))
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

    return class_methods, bool_members

def _unique_symbols(symbols):
    """The first symbol of each (name, kind, parent), constructors by their signature."""
    seen = set()
    unique = []
    for s in symbols:
        key = (s.name, s.kind, s.parent, s.detail if s.kind == 'Constructor' else '')
        if key not in seen:
            seen.add(key)
            unique.append(s)
    return unique


def generate_header(symbols, output_path):
    """Generate engine_api_suggestions.h from parsed symbols."""
    unique = _unique_symbols(symbols)

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

    # ── Properties that hold true or false, as "Class.name" ──────────────
    lines.append('inline std::unordered_set<std::string> getEngineBoolProperties() {')
    lines.append('    return {')
    for name in sorted(set(f'{s.parent}.{s.name}' for s in unique if s.boolean)):
        lines.append(f'        "{escape(name)}",')
    lines.append('    };')
    lines.append('}')
    lines.append('')

    # ── Events scripts register for, as "Class.name" ─────────────────────
    lines.append('inline std::unordered_set<std::string> getEngineEventProperties() {')
    lines.append('    return {')
    for name in sorted(set(f'{s.parent}.{s.name}' for s in events)):
        lines.append(f'        "{escape(name)}",')
    lines.append('    };')
    lines.append('}')
    lines.append('')

    # ── The bound symbols in C++, by "call/", "prop/" or "enum/" ──────────
    lines.append('struct EngineCppSymbol {')
    lines.append('    const char* key;')
    lines.append('    const char* read;   // a member, a call or a value')
    lines.append('    const char* write;  // a setter, or = for a data member')
    lines.append('    const char* result; // * for a pointer, & for a reference it can change')
    lines.append('};')
    lines.append('')
    lines.append('inline const std::vector<EngineCppSymbol>& getEngineCppSymbols() {')
    lines.append('    static const std::vector<EngineCppSymbol> symbols = {')
    cpp_groups = {'Method': 'call', 'StaticMethod': 'call', 'Property': 'prop', 'Constant': 'prop', 'EnumMember': 'enum'}
    cpp_seen = set()
    for s in unique:
        group = cpp_groups.get(s.kind)
        if not group or not s.cpp_read or not s.parent:
            continue
        key = f'{group}/{s.parent}.{s.name}'
        if key in cpp_seen:
            continue
        cpp_seen.add(key)
        lines.append(f'        {{"{escape(key)}", "{escape(s.cpp_read)}", "{escape(s.cpp_write)}", "{s.cpp_result}"}},')
    lines.append('    };')
    lines.append('    return symbols;')
    lines.append('}')
    lines.append('')

    # ── The header of each bound class and enum, from the engine core ────
    lines.append('struct EngineTypeHeader {')
    lines.append('    const char* name;')
    lines.append('    const char* header;')
    lines.append('};')
    lines.append('')
    lines.append('inline const std::vector<EngineTypeHeader>& getEngineTypeHeaders() {')
    lines.append('    static const std::vector<EngineTypeHeader> headers = {')
    listed = {}
    for name in class_names:
        if name in type_headers:
            listed[name] = type_headers[name]
    for s in enum_members:
        # LightType::POINT, or Plane::Side::BOTH of a class
        owner = s.cpp_read.split('::')[0]
        if s.parent not in listed and owner in type_headers:
            listed[s.parent] = type_headers[owner]
    for name in sorted(listed):
        lines.append(f'        {{"{escape(name)}", "{escape(listed[name])}"}},')
    lines.append('    };')
    lines.append('    return headers;')
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
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))

    print(f'[generate_api_suggestions] Generated {output_path}')
    print(f'  Classes: {len(classes)}, Methods: {len(methods)}, '
          f'CppMethods: {len(cpp_methods_only)}, '
          f'StaticMethods: {len(static_methods)}, Properties: {len(properties)}, '
          f'Constants: {len(constants)}, Events: {len(events)}, '
          f'Enums: {len(enums)}, EnumMembers: {len(enum_members)}')


# ── Lua stubs for lua-language-server ─────────────────────────────────────

LUA_INTEGERS = {
    'int', 'unsigned', 'unsigned int', 'long', 'unsigned long', 'long long', 'unsigned long long',
    'short', 'unsigned short', 'unsigned char', 'wchar_t', 'size_t', 'Entity',
    'int8_t', 'int16_t', 'int32_t', 'int64_t', 'uint8_t', 'uint16_t', 'uint32_t', 'uint64_t',
}
LUA_KEYWORDS = {
    'and', 'break', 'do', 'else', 'elseif', 'end', 'false', 'for', 'function', 'global', 'goto', 'if',
    'in', 'local', 'nil', 'not', 'or', 'repeat', 'return', 'then', 'true', 'until', 'while',
}
RE_CALLBACK = re.compile(r'^(?:FunctionSubscribe|function)\s*<(.*)>$')
# Lua classes of the events, which the class parser leaves out
RE_EVENT_CLASS = re.compile(r'\.beginClass\s*<\s*FunctionSubscribe\s*<(.*?)>\s*>\s*\(\s*"([^"]+)"\s*\)')


def _callback_signature(signature, names):
    """LuaLS (name, type) parameters and return type of a C++ signature like void(int, float)."""
    m = re.match(r'^(.*?)\s*\((.*)\)$', signature.strip())
    params = [p for p in _split_types_raw(m.group(2)) if p.strip() not in ('', 'void')]
    return [(f'a{i + 1}', _lua_type(p, names) or 'any') for i, p in enumerate(params)], _lua_type(m.group(1), names)


def _fun_type(params, returns):
    return 'fun(' + ', '.join(f'{p}: {t}' for p, t in params) + ')' + (f': {returns}' if returns else '')


def _lua_type(cpp, names):
    """LuaLS type of a C++ type, '' for void, 'any' when Lua gets nothing known."""
    pointer_to_char = re.search(r'\bchar\s*\*', cpp)
    t = re.sub(r'\b(?:std|doriax)::', '', cpp)
    t = re.sub(r'\b(?:const|volatile|virtual|static|inline|constexpr|struct|class|typename)\b', ' ', t)
    t = re.sub(r'\s+', ' ', re.sub(r'[&*]', ' ', t)).strip()
    # LuaBridge gives a char as a one-letter string
    if pointer_to_char or t in ('string', 'wstring', 'char'):
        return 'string'
    if t in ('', 'void'):
        return ''
    if t == 'bool':
        return 'boolean'
    if t in ('float', 'double'):
        return 'number'
    if t in LUA_INTEGERS:
        return 'integer'
    # an event Lua has a class of
    if t.replace(' ', '') in names:
        return names[t.replace(' ', '')]
    m = RE_CALLBACK.match(t)
    if m:
        return _fun_type(*_callback_signature(m.group(1), names))
    m = re.match(r'^(?:vector|array|list|deque|set|unordered_set)\s*<(.*)>$', t)
    if m:
        return (_lua_type(_split_types_raw(m.group(1))[0], names) or 'any') + '[]'
    m = re.match(r'^(?:map|unordered_map)\s*<(.*)>$', t)
    if m:
        key, value = (_split_types_raw(m.group(1)) + ['', ''])[:2]
        return f'table<{_lua_type(key, names) or "any"}, {_lua_type(value, names) or "any"}>'
    return names.get(t) or names.get(t.split('::')[-1]) or 'any'


def _lua_params(params, names):
    """(name, type, optional) of a simplified C++ parameter list."""
    result = []
    for i, param in enumerate(p for p in _split_types_raw(params) if p.strip()):
        declaration, _, default = param.partition('=')
        parts = declaration.strip().rsplit(' ', 1)
        cpp_type, name = (parts[0], parts[1]) if len(parts) == 2 else (parts[0], '')
        if 'lua_State' in cpp_type:
            continue
        if cpp_type == '...':
            result.append(('...', 'any', False))
            continue
        if not re.match(r'^[A-Za-z_][A-Za-z0-9_]*$', name):
            name = f'p{i + 1}'
        if name in LUA_KEYWORDS:
            name += '_'
        result.append((name, _lua_type(cpp_type, names) or 'any', bool(default.strip())))
    return result


def _lua_comment(text):
    return [f'---{line}' for line in text.split('\n')] if text else []


def _lua_function(owner, sep, name, params, returns, doc, names):
    lines = _lua_comment(doc)
    lua_params = _lua_params(params, names)
    for param, lua_type, optional in lua_params:
        lines.append(f'---@param {param}{"?" if optional else ""} {lua_type}')
    lua_return = _lua_type(returns, names)
    if lua_return:
        lines.append(f'---@return {lua_return}')
    lines.append(f'function {owner}{sep}{name}({", ".join(p[0] for p in lua_params)}) end')
    return lines


def _member_type(accessor, cpp_methods):
    """C++ type a property reads through its accessor, '' when unknown."""
    m = re.search(r'constantCopy<\s*([A-Za-z0-9_:]+)', accessor)
    if m:
        return m.group(1)
    m = RE_LAMBDA_SIGNATURE.search(accessor)
    if m:
        return m.group(2)
    ret = _getter_return_type(accessor, cpp_methods)
    if ret:
        return ret
    m = re.search(r'&\s*([A-Za-z0-9_]+)::([A-Za-z0-9_]+)', accessor)
    if not m:
        # a lambda returning &self->onPress
        m = re.search(r'^\[\]\s*\(\s*(?:const\s+)?([A-Za-z0-9_]+)\s*[*&].*?return\s*&?\s*self->([A-Za-z0-9_]+)', accessor)
    if m:
        return member_types.get(m.group(1), {}).get(m.group(2), '')
    return ''


LUA_PRELUDE = [
    '---@meta doriax',
    '-- Doriax engine API for lua-language-server, generated from the engine bindings.',
    '',
    '---A Lua script attached to an entity, as self in its methods',
    '---@class LuaScript',
    '---@field scene Scene',
    '---@field entity integer',
    '',
    '---Calls self:methodName on an engine event, such as "onUpdate"',
    '---@param script LuaScript',
    '---@param methodName string',
    '---@param tag? string',
    'function RegisterEngineEvent(script, methodName, tag) end',
    '',
    '---Calls self:methodName when a component event fires, such as a button\'s onClick',
    '---@param script LuaScript',
    '---@param event any',
    '---@param methodName string',
    '---@param tag? string',
    'function RegisterEvent(script, event, methodName, tag) end',
    '',
]


def _lua_enum(name, members, docs):
    lines = _lua_comment(docs.get(name, {}).get('description', ''))
    lines.append(f'---@enum {name}')
    lines.append(f'{name} = {{')
    for i, member in enumerate(members):
        owner, _, value_name = member.cpp_read.rpartition('::')
        value = enum_values.get(owner.split('::')[-1], {}).get(value_name)
        lines.append(f'    {member.name} = {value if value is not None else i},')
    return lines + ['}', '']


def _lua_event(name, signature, names):
    """An engine event: called as a function, and add subscribes one more."""
    params, returns = _callback_signature(signature, names)
    lines = [
        f'---@class {name}',
        f'---@overload {_fun_type(params, returns)}',
        f'{name} = {{}}',
        '',
        '---Calls func each time the event is called, under a tag',
        '---@param tag string',
        f'---@param func {_fun_type(params, returns)}',
        '---@return boolean',
        f'function {name}:add(tag, func) end',
        '',
        '---Calls every function added',
    ]
    lines += [f'---@param {p} {t}' for p, t in params]
    if returns:
        lines.append(f'---@return {returns}')
    lines.append(f'function {name}:call({", ".join(p for p, _ in params)}) end')
    return lines + ['']


def _lua_class(name, cls, members, doc, names, cpp_methods, callbacks, types):
    """A class with its operators, fields and constructors, and its table."""
    member_docs = doc.get('members', {})
    lines = _lua_comment(doc.get('description', ''))
    if doc.get('url'):
        lines.append(f'---[Doriax reference]({doc["url"]})')
    base = names.get(cls.base, '') if cls.base else ''
    lines.append(f'---@class {name}' + (f': {base}' if base else ''))

    for s in members:
        if s.kind != 'Operator':
            continue
        op = LUA_OPERATORS[s.name]
        if op == 'unm':
            lines.append(f'---@operator unm: {name}')
            continue
        symbol = {'add': '+', 'sub': '-', 'mul': '*', 'div': '/'}[op]
        for overload in s.overloads:
            operand = overload[0] if overload else ''
            result = operator_returns.get(cls.cpp_class, {}).get((symbol, operand)) or name
            lines.append(f'---@operator {op}({_lua_type(operand, names) or "any"}): {_lua_type(result, names) or name}')

    for s in members:
        if s.kind not in ('Property', 'Constant', 'Event') or s.name in LUA_KEYWORDS:
            continue
        lua_type = _lua_type(_member_type(s.accessor, cpp_methods), names) or 'any'
        # an event also takes a function, as btn.onPress = function() end
        if lua_type in callbacks and 'lua_State' in s.mutator:
            lua_type += '|' + callbacks[lua_type]
        types.append(lua_type)
        notes = member_docs.get(s.name, '')
        if s.readonly or s.kind == 'Constant':
            notes = ('Read-only. ' + notes).strip()
        elif s.detail.endswith('(copy)'):
            notes = ('A copy: set the whole value to change it. ' + notes).strip()
        lines.append(f'---@field {s.name} {lua_type}' + (f' {notes}' if notes else ''))

    for s in members:
        if s.kind != 'Constructor':
            continue
        params = s.detail[s.detail.find('(') + 1:-1]
        bound = [_param_type(p) for p in _split_types(params)]
        header = next((c for c in constructors.get(cls.cpp_class or name, [])
                       if [_param_type(p) for p in _split_types(c)] == bound), params)
        lua_params = ', '.join(f'{p}{"?" if o else ""}: {t}' for p, t, o in _lua_params(header, names))
        lines.append(f'---@overload fun({lua_params}): {name}')
    return lines + [f'{name} = {{}}', '']


def _lua_methods(name, members, doc, names, cpp_methods, types):
    """A definition of each method, and one more of each other overload Lua gets."""
    member_docs = doc.get('members', {})
    lines = []
    for s in members:
        if s.kind not in ('Method', 'StaticMethod'):
            continue
        sep = '.' if s.kind == 'StaticMethod' else ':'
        params, returns = s.lua_signature or ('...', '')
        # getComponent<FogComponent> or getSystem<UISystem>
        template = re.match(r'^get[A-Za-z]*<([A-Za-z0-9_:]+)>$', s.callee[1]) if s.callee else None
        if template:
            params, returns = '', template.group(1)
        if params == '...':
            types.append('any')
            lines += _lua_comment(member_docs.get(s.name, ''))
            lines.append('---@param ... any')
            lines.append(f'function {name}{sep}{s.name}(...) end')
        else:
            types.append(_lua_type(returns, names) or 'void')
            lines += _lua_function(name, sep, s.name, params, returns, member_docs.get(s.name, ''), names)
        overloads = cpp_methods.get(s.callee[0], {}).get(s.callee[1], []) if s.callee else []
        for bound in s.overloads:
            other = _bound_overload(overloads, bound)
            if other[0] != params:
                lines += _lua_function(name, sep, s.name, other[0], other[1] or returns, '', names)
        lines.append('')
    return lines


def generate_lua_stubs(symbols, events, cpp_methods, docs, output_path):
    """Write a lua-language-server definition file of the bound API and its events."""
    symbols = _unique_symbols(symbols)
    classes = {s.name: s for s in symbols if s.kind == 'Class'}
    enums = {}
    members = {}
    for s in symbols:
        if s.kind == 'EnumMember' and s.parent not in classes:
            enums.setdefault(s.parent, []).append(s)
        elif s.parent in classes and s.kind not in ('Class', 'CppMethod', 'EnumMember'):
            members.setdefault(s.parent, []).append(s)
    names = {name: name for name in classes}
    names.update({cls.cpp_class: name for name, cls in classes.items() if cls.cpp_class})
    names.update({name: name for name in enums})
    names.update({f'FunctionSubscribe<{signature}>': name for signature, name in events.items()})
    callbacks = {name: _fun_type(*_callback_signature(signature, names)) for signature, name in events.items()}

    types = []  # every type written, to tell how many are known
    lines = list(LUA_PRELUDE)
    for name in sorted(enums):
        lines += _lua_enum(name, enums[name], docs)
    for signature, name in sorted(events.items(), key=lambda item: item[1]):
        lines += _lua_event(name, signature, names)
    for name in sorted(classes):
        doc = docs.get(name, {})
        lines += _lua_class(name, classes[name], members.get(name, []), doc, names, cpp_methods, callbacks, types)
        lines += _lua_methods(name, members.get(name, []), doc, names, cpp_methods, types)

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))
    print(f'[generate_api_suggestions] Generated {output_path} ({len(types) - types.count("any")} typed, {types.count("any")} any)')


# ── Doc comments in the engine headers copied next to the editor ──────────

RE_CLASS_LINE = re.compile(r'^\s*(?:class|struct)\s+(?:DORIAX_API\s+)?([A-Za-z0-9_]+)\b')
RE_MEMBER_LINE = re.compile(r'^\s*(?:(?:virtual|static|inline|explicit|constexpr)\s+)*[A-Za-z_][\w:<>,\s*&]*?\b([A-Za-z_]\w*)\s*[(=;]')


def _doc_lines(indent, text):
    return [f'{indent}/// {line}' for line in textwrap.wrap(text, 96)]


def _annotate_header(text, class_docs):
    """The header with /// comments above each documented class and member."""
    out = []
    depth = 0
    stack = []  # (class, depth of its body)
    pending = None
    for line in text.split('\n'):
        code = re.sub(r'//.*', '', line)
        indent = line[:len(line) - len(line.lstrip())]
        # never inside a macro continued over lines
        documented = out and (out[-1].strip().startswith('///') or out[-1].rstrip().endswith('\\'))
        m = RE_CLASS_LINE.match(code)
        current = stack[-1][0] if stack and stack[-1][1] == depth else None
        if m and m.group(1) in class_docs and not code.rstrip().endswith(';'):
            pending = m.group(1)
            description, url, _ = class_docs[pending]
            if not documented and (description or url):
                out += _doc_lines(indent, description) + ([f'{indent}/// {url}'] if url else [])
        elif current and not documented:
            m = RE_MEMBER_LINE.match(code)
            if m and m.group(1) in class_docs[current][2]:
                out += _doc_lines(indent, class_docs[current][2][m.group(1)])
        out.append(line)
        for c in code:
            if c == '{':
                depth += 1
                if pending:
                    stack.append((pending, depth))
                    pending = None
            elif c == '}':
                if stack and stack[-1][1] == depth:
                    stack.pop()
                depth -= 1
        if code.rstrip().endswith(';'):
            pending = None
    return '\n'.join(out)


def annotate_headers(symbols, cpp_methods, docs, engine_core_dir, overlay_dir):
    """Write documented copies of the engine headers under overlay_dir."""
    classes = {s.name: s for s in symbols if s.kind == 'Class'}
    # C++ class -> (description, url, {member: text})
    class_docs = {}
    def add(cpp_class, member, text):
        class_docs.setdefault(cpp_class, ('', '', {}))[2].setdefault(member, text)

    for name, doc in docs.items():
        cpp_class = classes[name].cpp_class if name in classes else name
        class_docs[cpp_class] = (doc.get('description', ''), doc.get('url', ''), {})
        for member, text in doc.get('members', {}).items():
            if member in cpp_methods.get(cpp_class, {}) or member in member_types.get(cpp_class, {}):
                add(cpp_class, member, text)

    # a Lua name documents the C++ members it is bound to
    for s in symbols:
        text = docs.get(s.parent, {}).get('members', {}).get(s.name)
        if not text or s.parent not in classes:
            continue
        if s.kind in ('Method', 'StaticMethod') and s.callee:
            add(s.callee[0], s.callee[1].split('<')[0], text)
        elif s.kind in ('Property', 'Constant', 'Event'):
            for owner, member in re.findall(r'&\s*([A-Za-z0-9_]+)::([A-Za-z0-9_]+)', s.accessor + ' ' + s.mutator):
                add(owner, member, text)

    headers = {}
    for cpp_class in class_docs:
        if cpp_class in type_headers:
            headers.setdefault(type_headers[cpp_class], []).append(cpp_class)

    root = os.path.basename(os.path.normpath(engine_core_dir))
    for relative in sorted(headers):
        with open(os.path.join(engine_core_dir, relative), encoding='utf-8') as f:
            text = f.read()
        annotated = _annotate_header(text, class_docs)
        if annotated != text:
            path = os.path.join(overlay_dir, root, relative)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, 'w', encoding='utf-8', newline='') as f:
                f.write(annotated)
    print(f'[generate_api_suggestions] Documented {len(headers)} engine headers')


def parse_event_classes(binding_files):
    """Lua class of each FunctionSubscribe signature, keyed by the signature without spaces."""
    events = {}
    for filepath in binding_files:
        with open(filepath, encoding='utf-8') as f:
            for signature, name in RE_EVENT_CLASS.findall(f.read()):
                events.setdefault(signature.replace(' ', ''), name)
    return events


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
    # Lua stubs and documented headers, with the docs they take
    overlay_dir = sys.argv[4] if len(sys.argv) > 4 else ''
    docs_path = sys.argv[5] if len(sys.argv) > 5 else ''

    binding_files = sorted(glob.glob(os.path.join(binding_dir, '*.cpp')))
    if not binding_files:
        print(f'WARNING: No binding files found in {binding_dir}')
        sys.exit(1)

    all_symbols = []
    for filepath in binding_files:
        syms = parse_binding_file(filepath)
        all_symbols.extend(syms)
        print(f'  Parsed {os.path.basename(filepath)}: {len(syms)} symbols')

    cpp_methods, bool_members = parse_cpp_headers(engine_core_dir)

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
            sig = s.signature or cpp_signatures.get((s.parent, s.name)) or cpp_signatures.get((s.parent, s.getter))
            # the header's shortest overload is not always the one bound
            if s.signature is None and s.bound is not None:
                sig = _bound_overload(cpp_methods.get(s.parent, {}).get(s.name, []), s.bound)
            s.lua_signature = sig
            if sig is not None:
                sep = '.' if s.kind == 'StaticMethod' else ':'
                s.detail = _format_detail(s.parent, s.name, sep, sig)

    lua_classes = {s.name for s in all_symbols if s.kind == 'Class'}

    for s in all_symbols:
        if s.kind == 'Property' and s.accessor:
            s.boolean = _is_bool_property(s.accessor, cpp_methods, bool_members)

    method_names = {name for methods_dict in cpp_methods.values() for name in methods_dict}
    for s in all_symbols:
        if s.kind in ('Property', 'Constant') and s.accessor:
            s.cpp_read, s.cpp_write = _cpp_property(s, cpp_methods, method_names, bool_members)

    # a logic graph reaches a pointer's members with -> and keeps a reference as one
    for s in all_symbols:
        if s.kind in ('Method', 'StaticMethod') and s.callee:
            s.cpp_result = _result_kind(_callee_return(s, cpp_methods))
        elif s.kind in ('Property', 'Constant') and s.cpp_read:
            member = _member_pointer(s.accessor)
            if member and member[1] in pointer_members.get(member[0], set()):
                s.cpp_result = '*'
            else:
                s.cpp_result = _result_kind(_getter_return_type(s.accessor, cpp_methods))

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

    if overlay_dir:
        docs = {}
        if docs_path and os.path.exists(docs_path):
            with open(docs_path, encoding='utf-8') as f:
                docs = json.load(f)
        shutil.rmtree(overlay_dir, ignore_errors=True)
        generate_lua_stubs(all_symbols, parse_event_classes(binding_files), cpp_methods, docs,
                           os.path.join(overlay_dir, 'lua-api', 'doriax.lua'))
        annotate_headers(all_symbols, cpp_methods, docs, engine_core_dir, overlay_dir)


if __name__ == '__main__':
    main()
