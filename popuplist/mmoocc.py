# vim: set fileencoding=utf-8 :vim
# Minimal Object-Oriented-C Compiler
#    - parses comments that start with /* [ooc]; parses until the first */
#    - parses class and struct definitions
#    - creates typedefs for structs and classes
#    - creates "virtual" function tables for classes
#    - class instances have a pointer op to their virtual function tables
#    - struct instances contain only data and no vf tables, but can be inherited
#      from other structs so that the fields of the base struct are at the
#      beginning of the derived struct;
#    - creates skeletons for methods of structs and classes
#    - creates a function that initializes the virtual tables
#    - creates initialization code for non-pointer members (of known types)
#
# Author: Marko Mahnič
# Created: March 2011
import os, sys
import re

rxooc = re.compile(r"^\s*/\*\s*\[ooc\]")
rxclass = re.compile(
    r"^\s*(class|struct)\s+([a-zA-Z0-9_]+)"
    + r"(?:\s*\(\s*([a-zA-Z0-9_]+)\s*\))?"    # Base
    + r"(?:\s*\[(.*)\])?")                    # Options [fn_prefix, type_name, ...]
rxconst = re.compile(r"^\s*const\s+([a-zA-Z0-9_]+)\s*=\s*(.+)\s*;")
rxtypedef = re.compile(r"^\s*typedef\s+\S+\s+\S+")
rxmethod = re.compile(r"^\s*(.*)\s+([a-zA-Z0-9_]+)\s*\((.*)\)\s*;")
rxmember = re.compile(r"^\s*(.*)\s+([a-zA-Z0-9_]+)\s*(\[.*\])?\s*;")
rxarray  = re.compile(r"^\s*array\s*\((.*)\)")

classes = []  # will be converted to structs
consts  = []  # will be converted to #define-s
read_order = []
DEBUG = False
STATIC_VT_INIT = True

def find_class(name):
    global classes
    for c in classes:
        if c.name == name:
            return c
    return None

def normalize_decl(decl):
    decl = decl.replace("*", "* ").split()
    ps = []
    prev = ""
    for tok in decl:
        if tok.startswith("*"):
            prev += tok
        else:
            if len(prev): ps.append(prev)
            prev = tok
    if len(prev): ps.append(prev)
    return " ".join(ps)

# Replace class types in declarations (parameters, members, return values)
def replace_known_type(decl, replace_cb):
    ps = normalize_decl(decl).split()
    param = []
    for tok in ps:
        ptr = 0
        while tok.endswith("*"):
            tok = tok[:-1]
            ptr += 1
        c = find_class(tok)
        if c != None:
            tok = replace_cb(c)
        tok += ("*" * ptr)
        param.append(tok)
    return " ".join(param)

fout = None
indent = 0
def writeln(s):
    global fout, indent
    s = s.split("\n")
    for l in s:
        if l.find("}") >= 0: indent -= 1
        ls = l.strip()
        if ls.startswith("#") or ls.startswith("/*-") or ls.startswith("-*/"):
            fout.write("%s\n" % (ls));
        else:
            fout.write("%s%s\n" % (" " * (indent*4), l));
        if l.find("{") >= 0: indent += 1
    pass

class CConst(object):
    def __init__(self, name, value):
        self.name = name
        self.value = value

    def dump(self):
        writeln("#define %s %s" % (self.name, self.value))

class CTypedef(object):
    def __init__(self, inputLine):
        self.typedef = inputLine.strip()

    def dump(self):
        writeln("%s\n" % self.typedef)


class CMember:
    def __init__(self, name, type, array=None): # array = []
        self.name = name.strip()
        self.type = type.strip()
        self.array = array
        self.is_array = False     # array(class) types are treated specially
        self.np_class = None      # a non-pointer member of a known class

    # called after all things are parsed
    def resolve_type(self):
        # check if it's an array
        amo = rxarray.match(self.type)
        if amo != None:
            self.is_array = True
            itm = amo.group(1).strip()
            c = find_class(itm)
            if c != None: self.array_type = c.object_type
            else: self.array_type = itm

        # check if it's a non-pointer member of a known class
        parts = normalize_decl(self.type).split()
        for tok in parts:
            if tok.endswith("*"): continue
            c = find_class(tok)
            if c != None:
                self.np_class = c
                break


    @property
    def declaration_type(self):
        p = replace_known_type(self.type, lambda c:  "struct %s" % c.object_struct)
        return p

class CMethod:
    def __init__(self, name, result, params):
        self.name = name.strip()
        self.result = result.strip()
        if params == None: self.params = None
        else:
            self.params = [ p.strip() for p in params.split(",") ]

    def __repr__(self):
        return "<Method %s>" % self.name

    @property
    def declaration_type(self):
        return replace_known_type(self.result, lambda c: "struct %s" % c.object_struct)

    @property
    def declaration_args(self):
        args = []
        # Expand known class and struct names to forward-declared names
        for p in self.params:
            p = replace_known_type(p, lambda c:  "struct %s" % c.object_struct)
            args.append(p)
        return args

    @property
    def body_type(self):
        return replace_known_type(self.result, lambda c: "%s" % c.object_type)

    @property
    def body_args(self):
        args = []
        # Expand known class and struct names to typedef-ed names
        for p in self.params:
            p = replace_known_type(p, lambda c:  "%s" % c.object_type)
            args.append(p)
        return args


class CClass(object):
    def __init__(self, name="", base=None, options=None):
        self.name = name
        self.baseClass = base
        self.fn_prefix = self.name[:4]
        # self.type_name = None
        self.variant = None
        if options != None:
            self._parse_options(options)
        self.members = []
        self.methods = []
        self.thisname = "self"
        self.vtable_ptr = "op"

    def __repr__(self):
        return "<Class %s>" % self.name

    def _parse_options(self, options):
        iopts = 0
        for opt in options:
            tv = opt.split()
            if len(tv) == 1:
                if iopts == 0: self.fn_prefix = opt
                if iopts == 1: self.type_name = opt
            elif len(tv) == 2:
                if tv[0] == 'prefix': self.fn_prefix = tv[1]
                # if tv[0] == 'type': self.type_name = tv[1]
                if tv[0] == 'variant': self.variant = tv[1]
            else:
                print "    Bad class option:", opt

    @property
    def class_struct(self):
        return "_%s_c" % self.name

    @property
    def class_type(self):
        return "_%s_VT" % self.name

    @property
    def class_vtable(self):
        return "_vt_%s" % self.name

    @property
    def object_struct(self):
        return "_%s_o" % self.name

    @property
    def object_type(self):
        return "%s_T" % self.name

    def method_name(self, name):
        mn = "_%s_%s" % (self.fn_prefix, name)
        if len(mn) > 31:
            print "WARN: Method name too long (%d): %s" % (len(mn), mn)
        return mn

    def hasMethod(self, name):
        for m in self.methods:
            if m.name == name: return True
        return False

    def resolve_base(self):
        if self.baseClass == None: return
        if self.baseClass == "object":
            self.baseClass = None
            return
        c = find_class(self.baseClass)
        if c != None:
            self.baseClass = c
            return

        print "%s: Unknown base class %s" % (self.name, self.baseClass)

    # called after all things are parsed
    def resolve_types(self):
        self.resolve_base()
        for m in self.members:
            m.resolve_type()

    def dump_forward_decl(self):
        if self.variant:
            writeln("#ifdef %s" % self.variant)
        writeln("typedef struct %s %s;" % (self.object_struct, self.object_struct));
        if self.variant:
            writeln("#endif")

    def dump_members(self):
        if self.baseClass != None:
            self.baseClass.dump_members()
        for m in self.members:
            if m.is_array: typ = "garray_T"
            else: typ = m.declaration_type
            writeln("%s %s%s;" % (typ, m.name, m.array if m.array != None else ""))

    def dump_method_ptrs(self):
        if self.baseClass != None:
            self.baseClass.dump_method_ptrs()
        # thistype = self.class_type # can't forward-declare the data-type structure :(
        thistype = "void" # % self.object_type
        for m in self.methods:
            if m.name == "init":
                continue # op is not set before init() is called
            if self.baseClass and self.baseClass.hasMethod(m.name):
                continue # method override
            params = ["%s* _%s" % (thistype, self.thisname)] + m.declaration_args
            params = [p.strip() for p in params if p.strip() != ""]
            params = ", ".join(params)
            writeln("%s (*%s)(%s);" % (m.declaration_type, m.name, params))

    def dump_method_decls(self):
        thistype = "/*%s*/void" % self.object_type
        for m in self.methods:
            params = ["%s* _%s" % (thistype, self.thisname)] + m.declaration_args
            params = [p.strip() for p in params if p.strip() != ""]
            params = ", ".join(params)
            writeln("static %s %s __ARGS((%s));" % (m.declaration_type, self.method_name(m.name), params))

    def get_vt_methods(self):
        methods = [] # list of [method, class]
        if self.baseClass:
            methods += self.baseClass.get_vt_methods()
        for m in self.methods:
            found = False
            for pair in methods:
                if pair[0].name == m.name: # method override
                    pair[1] = self
                    found = True
            if not found:
                methods.append([m, self])
        return methods

    def dump_vtable_static_init(self):
        writeln("static %s %s = {" % (self.class_type, self.class_vtable))
        vt = self.get_vt_methods()
        for pair in vt:
            if pair[0].name == "init": continue
            writeln("&%s," % (pair[1].method_name(pair[0].name)))
        writeln("};")

    def dump_vtable_init(self):
        writeln("static %s %s;" % (self.class_type, self.class_vtable))
        writeln("    void")
        writeln("_vtinit_%s(_class)" % (self.name))
        writeln("    %s* _class;" % (self.class_type))
        writeln("{")
        if self.baseClass != None:
            writeln("_vtinit_%s((%s*)_class);" % (self.baseClass.name, self.baseClass.class_type))
        for m in self.methods:
            if m.name == "init": continue
            writeln("_class->%s = &%s;" % (m.name, self.method_name(m.name)))
        writeln("}")


    def self_cast_expr(self):
        return "CAST_CLASS(%s, %s);" % (self.thisname, self.name)


    def dump_constructor(self, set_vtable=True):
        if self.baseClass:
            base = self.baseClass.find_methods_super_class("init")
            if base != None:
                writeln("    %s(_%s);\\" % (base.method_name("init"), base.thisname));
        if set_vtable and self.vtable_ptr != None:
            writeln("    self->%s = &%s;\\" % (self.vtable_ptr, self.class_vtable));
        # TODO: construct non-pointer structures
        for m in self.members:
            if m.is_array:
                writeln("    ga_init2(&%s->%s, sizeof(%s), 128);\\" % (self.thisname, m.name, m.array_type))
            if m.np_class != None:
                c = m.np_class.find_methods_super_class("init")
                if c != None:
                    writeln("    /* INIT %s %s */\\" % (m.name, m.np_class.name))
                    writeln("    %s(&self->%s);\\" % (c.method_name("init"), m.name));


    def dump_destructor(self):
        writeln("") # end _BODY
        writeln("#define _%s_DESTROY() \\" % self.name)
        # TODO: destroy non-pointer structures
        for m in self.members:
            if m.is_array:
                writeln("    ga_clear(&%s->%s);\\" % (self.thisname, m.name))
            if m.np_class != None:
                c = m.np_class.find_methods_super_class("destroy")
                if c != None:
                    writeln("    /* DESTROY %s %s */\\" % (m.name, m.np_class.name))
                    writeln("    %s(&self->%s);\\" % (c.method_name("destroy"), m.name));
        if self.baseClass != None:
            base = self.baseClass.find_methods_super_class("destroy")
            if base != None:
                writeln("    %s(_%s);\\" % (base.method_name("destroy"), base.thisname));


    def find_methods_super_class(self, method):
        if self.hasMethod(method):
            return self

        if self.baseClass:
            return self.baseClass.find_methods_super_class(method)

        return None

    def dump_method_bodies(self):
        if self.variant:
            writeln("#ifdef %s" % self.variant)

        thistype = "void" # % self.object_type
        for m in self.methods:
            params = ["%s* _%s" % (thistype, self.thisname)] + m.body_args
            params = [p.strip() for p in params if p.strip() != ""]
            args = [p.split()[-1] for p in params]
            args = ", ".join(args)

            msuper = None
            if self.baseClass:
                msuper = self.baseClass.find_methods_super_class(m.name)
            if msuper:
                writeln("#define _super_vt_%s_%s() %s\n"
                        % (self.name, m.name, self.baseClass.class_vtable))

            writeln("#define _%s_%s_BODY()\\" % (self.name, m.name))
            if DEBUG:
                writeln(self.self_cast_expr() + "\\")
            if m.name == "init": self.dump_constructor()
            if m.name == "destroy": self.dump_destructor()

            writeln("\n/*-")
            writeln("    static %s\n%s(%s)" % (m.body_type, self.method_name(m.name), args))
            for p in params:
                writeln("    %s;" % p)
            writeln("{")
            writeln("METHOD(%s, %s);" % (self.name, m.name))
            if m.name == "destroy":
                writeln("END_DESTROY(%s);" % self.name)
            writeln("}")
            writeln("-*/\n")

        # the 'new' methods are treated specially and are public
        # TODO: 2 boilerplates will be generated (new_XXXX and static _xx_new); merge into one
        if self.hasMethod("new"):
            writeln("/*-") # The user will define his own, but we create the boilerplate
        env = {'type': self.object_type, 'var': self.fn_prefix, 'name': self.name}
        writeln("    %(type)s*" % env)
        writeln("new_%(name)s()" % env)
        writeln("{")
        writeln("%(type)s* _%(var)s = (%(type)s*) alloc(sizeof(%(type)s));" % env)
        writeln("if (! _%(var)s)" % env)
        writeln("    return NULL;")
        writeln("_%(var)s_init(_%(var)s);" % env)
        writeln("return _%(var)s;" % env)
        writeln("}")
        if self.hasMethod("new"):
            writeln("-*/") # end boilerplate
        if self.variant:
            writeln("#endif /* %s */" % self.variant)
        writeln("")

    def dump(self):
        base = ("(%s)" % self.baseClass.name) if self.baseClass != None else ""
        writeln("/* ########## class %s%s ########## */" % (self.name, base))

        # structures
        if self.variant:
            writeln("#ifdef %s" % self.variant)
        writeln("typedef struct %s {" % self.class_struct)
        self.dump_method_ptrs()
        writeln("} %s;" % self.class_type)

        writeln("")
        writeln("typedef struct %s {" % self.object_struct)
        writeln("%s* %s;" % (self.class_type, self.vtable_ptr))
        self.dump_members()
        writeln("} %s;" % self.object_type)
        writeln("")

        # method headers
        self.dump_method_decls()
        writeln("")

        # initialization
        if STATIC_VT_INIT:
            self.dump_vtable_static_init()
        else:
            self.dump_vtable_init()

        found = self.find_methods_super_class("init")
        if found:
            writeln("#define init_%s(pvar) \\" % self.name)
            writeln("    _%s_init(pvar)" % found.fn_prefix)

        if self.variant:
            writeln("#endif /* %s */" % self.variant)
        writeln("")


# Structs are like classes, but they don't have a pointer to a VM table
class CStruct(CClass):
    def __init__(self, name="", base=None, options=None):
        super(CStruct, self).__init__(name, base, options)
        self.vtable_ptr = None

    def self_cast_expr(self):
        return "CAST_STRUCT(%s, %s);" % (self.thisname, self.name)

    def find_methods_super_class(self, method):
        return None

    def dump(self):
        if self.variant:
            writeln("#ifdef %s" % self.variant)
        base = ("(%s)" % self.baseClass.name) if self.baseClass != None else ""
        writeln("/* ########## struct %s%s ########## */" % (self.name, base))
        writeln("typedef struct %s {" % self.object_struct)
        self.dump_members()
        writeln("} %s;" % self.object_type)
        writeln("")

        # method headers
        self.dump_method_decls()

        if self.hasMethod("init"):
            writeln("#define init_%s(pvar) \\" % self.name)
            writeln("    _%s_init(pvar)" % self.fn_prefix)

        if self.variant:
            writeln("#endif /* %s */" % self.variant)
        writeln("")


def parse_class(lines, i, c):
    i += 1
    while i < len(lines):
        l = lines[i]
        if l.find("*/") >= 0:
            print i, "OOOPS: end of comment found in class definition."
            return i-1 # reread the line in parent
        if l.strip().startswith("}"):
            break
        if l.strip().startswith("{"):
            l = l.replace("{", " ")
        lc = l.split("//", 2)
        l = lc[0].strip()
        mo = rxmethod.match(l)
        if mo != None:
            #print "method", i, mo.group(1).strip(), mo.group(2), mo.group(3)
            c.methods.append( CMethod(mo.group(2).strip(), mo.group(1), mo.group(3)) )
        else:
            mo = rxmember.match(l)
            if mo != None:
                #print "member", i, mo.group(1).strip(), mo.group(2), mo.group(3)
                c.members.append( CMember(mo.group(2).strip(), mo.group(1), mo.group(3)) )
        if mo == None and l.strip() != "":
            print i, "UNKNOWN DECLARATION:", l
        i += 1

    return i

def handle_class_header(matchObj, lines, i):
    mo = matchObj
    typ = mo.group(1)
    clname = mo.group(2)
    clbase = mo.group(3)
    clopts = mo.group(4)
    if clopts != None:
        clopts = [ o.strip() for o in clopts.split(",") ]
    c = None
    if typ == "class":
        c = CClass(clname, clbase, clopts)
        print "   class", clname
    elif typ == "struct":
        print "   struct", clname
        c = CStruct(clname, clbase, clopts)
    else:
        print i, "   Unsupported type:", typ
    if c != None:
        classes.append(c)
        read_order.append(c)
        i = parse_class(lines, i, c)
    return i

def handle_const(matchObj, lines, i):
    mo = matchObj
    name = mo.group(1)
    value = mo.group(2)
    c = CConst(name, value)
    consts.append(c)
    read_order.append(c)
    # print "   const %s %s" % (name, value)
    return i

def handle_typedef(matchObj, lines, i):
    td = CTypedef(lines[i])
    read_order.append(td)
    return i

def parse_ooc(lines, i):
    i += 1
    while i < len(lines):
        l = lines[i]
        ls = l.strip()
        if ls.find("*/") >= 0: break
        if ls == "" or ls == "*" or ls.startswith("//"):
            i += 1
            continue
        mo = rxclass.match(l)
        if mo != None:
            i = handle_class_header(mo, lines, i)
        if mo == None:
            mo = rxconst.match(l)
            if mo != None:
                i = handle_const(mo, lines, i)
        if mo == None:
            mo = rxtypedef.match(l)
            if mo != None:
                i = handle_typedef(mo, lines, i)
        if mo == None:
            print i, "   Unsupported statement:", l.strip()
        i += 1
    return i

# Dump typedefs between startStruct and the next class or struct
def dump_typedefs(startStruct):
    found = False
    if startStruct == None:
        found = True # typedef before the first class
    for it in read_order:
        if it == startStruct:
            found = True
            continue
        if not found: continue
        if isinstance(it, CTypedef):
            it.dump()
        elif isinstance(it, CClass):
            break

if len(sys.argv) > 1 and sys.argv[1] == '--debug':
    print "Debug"
    DEBUG = True

lines =  open("puls_st.c", "r").readlines()
lines += open("popuplst.c", "r").readlines()
lines += open("puls_pb.c", "r").readlines()
lines += open("puls_pm.c", "r").readlines()
lines += open("puls_tw.c", "r").readlines()

fout  = open("popupls_.ci", "w")
writeln("/* vim: set ft=c sw=4 ts=8 et fileencoding=utf-8 :vim */")
writeln("/* NOTE: This file is auto-generated. DO NOT EDIT. The changes will be lost. */")
i = 0
while i < len(lines):
    l = lines[i]
    if rxooc.match(l) != None:
        print "ooc", i
        i = parse_ooc(lines, i)
        print "ooc-end", i
    i += 1

for c in classes:
    c.resolve_types()

for c in consts:
    c.dump()
writeln("")

for c in classes:
    c.dump_forward_decl()
writeln("")

dump_typedefs(None)
for c in classes:
    c.dump()
    dump_typedefs(c)

if not STATIC_VT_INIT:
    writeln("    static int")
    writeln("_init_vtables()")
    writeln("{")
    writeln("static int inited = 0;")
    writeln("if (inited)")
    writeln("    return 0;")
    for c in classes:
        if c.vtable_ptr == None: continue
        if c.variant:
            writeln("#ifdef %s" % c.variant)
        writeln("_vtinit_%s(&%s);" % (c.name, c.class_vtable))
        if c.variant:
            writeln("#endif")
    writeln("inited = 1;")
    writeln("return 1;")
    writeln("}")

# XXX: CAST_CLASS depends on CClass.thisname
if DEBUG:
    writeln("""
    #define METHOD(classname, name) \\
        _##classname##_##name##_BODY()

    #define END_DESTROY(classname) \\
        _##classname##_DESTROY()

    # define super(classname, methodname) \\
        _super_vt_##classname##_##methodname().methodname

    #define CAST_CLASS(var, classname) \\
        classname##_T* var = (classname##_T*) _##var

    #define CAST_STRUCT(var, structname) \\
        structname##_T* var = (structname##_T*) _##var

    #define CLASS_DELETE(pobj) \\
        if (pobj) \\
        { \\
           pobj->op->destroy(pobj); \\
           vim_free(pobj); \\
           pobj = NULL; \\
        }
    """)
else:
    writeln("""
    #define METHOD(classname, name) \\
        typedef classname##_T* SELF_T; \\
        _##classname##_##name##_BODY()

    #define END_DESTROY(classname) \\
        _##classname##_DESTROY()

    # define self \\
        ((SELF_T)_self)

    # define super(classname, methodname) \\
        _super_vt_##classname##_##methodname().methodname

    #define CAST_CLASS(var, classname) \\
        classname##_T* var = (classname##_T*) _##var

    #define CAST_STRUCT(var, structname) \\
        structname##_T* var = (structname##_T*) _##var

    #define CLASS_DELETE(pobj) \\
        if (pobj) \\
        { \\
           pobj->op->destroy(pobj); \\
           vim_free(pobj); \\
           pobj = NULL; \\
        }
    """)

if 1:
    writeln("")
    writeln("/* --------------------------- */")
    writeln("/* Skeletons for class methods */")
    writeln("/* --------------------------- */")
    for c in classes:
        writeln("/* %s */" % c.name)
        c.dump_method_bodies()

fout.close()
