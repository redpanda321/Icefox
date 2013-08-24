#!/usr/bin/env python
# header.py - Generate C++ header files from IDL.
#
# ***** BEGIN LICENSE BLOCK *****
# Version: MPL 1.1/GPL 2.0/LGPL 2.1
#
# The contents of this file are subject to the Mozilla Public License Version
# 1.1 (the "License"); you may not use this file except in compliance with
# the License. You may obtain a copy of the License at
# http://www.mozilla.org/MPL/
#
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
#
# The Original Code is mozilla.org code.
#
# The Initial Developer of the Original Code is
#   Mozilla Foundation.
# Portions created by the Initial Developer are Copyright (C) 2008
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#   Benjamin Smedberg <benjamin@smedbergs.us>
#
# Alternatively, the contents of this file may be used under the terms of
# either of the GNU General Public License Version 2 or later (the "GPL"),
# or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
# in which case the provisions of the GPL or the LGPL are applicable instead
# of those above. If you wish to allow use of your version of this file only
# under the terms of either the GPL or the LGPL, and not to allow others to
# use your version of this file under the terms of the MPL, indicate your
# decision by deleting the provisions above and replace them with the notice
# and other provisions required by the GPL or the LGPL. If you do not delete
# the provisions above, a recipient may use your version of this file under
# the terms of any one of the MPL, the GPL or the LGPL.
#
# ***** END LICENSE BLOCK *****

"""Print a C++ header file for the IDL files specified on the command line"""

import sys, os.path, re, xpidl

printdoccomments = False

if printdoccomments:
    def printComments(fd, clist, indent):
        for c in clist:
            fd.write("%s%s\n" % (indent, c))
else:
    def printComments(fd, clist, indent):
        pass

def firstCap(str):
    return str[0].upper() + str[1:]

def attributeParamName(a):
    return "a" + firstCap(a.name)

def attributeNativeName(a, getter):
    binaryname = a.binaryname is not None and a.binaryname or firstCap(a.name)
    return "%s%s" % (getter and 'Get' or 'Set', binaryname)

def attributeParamlist(a, getter):
    return "%s%s" % (a.realtype.nativeType(getter and 'out' or 'in'),
                     attributeParamName(a))

def attributeAsNative(a, getter):
        scriptable = a.isScriptable() and "NS_SCRIPTABLE " or ""
        params = {'scriptable': scriptable,
                  'binaryname': attributeNativeName(a, getter),
                  'paramlist': attributeParamlist(a, getter)}
        return "%(scriptable)sNS_IMETHOD %(binaryname)s(%(paramlist)s)" % params

def methodNativeName(m):
    return m.binaryname is not None and m.binaryname or firstCap(m.name)

def methodReturnType(m, macro):
    """macro should be NS_IMETHOD or NS_IMETHODIMP"""
    if m.notxpcom:
        return "%s_(%s)" % (macro, m.realtype.nativeType('in').strip())
    else:
        return macro

def methodAsNative(m):
    scriptable = m.isScriptable() and "NS_SCRIPTABLE " or ""

    return "%s%s %s(%s)" % (scriptable,
                            methodReturnType(m, 'NS_IMETHOD'),
                            methodNativeName(m),
                            paramlistAsNative(m.params,
                                              m.realtype,
                                              notxpcom=m.notxpcom))

def paramlistAsNative(l, rettype, notxpcom, empty='void'):
    l = list(l)
    if not notxpcom and rettype.name != 'void':
        l.append(xpidl.Param(paramtype='out',
                             type=None,
                             name='_retval',
                             attlist=[],
                             location=None,
                             realtype=rettype))

    if len(l) == 0:
        return empty

    return ", ".join([paramAsNative(p) for p in l])

def paramAsNative(p):
    if p.paramtype == 'in':
        typeannotate = ''
    else:
        typeannotate = ' NS_%sPARAM' % p.paramtype.upper()

    return "%s%s%s" % (p.nativeType(),
                       p.name,
                       typeannotate)

def paramlistNames(l, rettype, notxpcom):
    names = [p.name for p in l]
    if not notxpcom and rettype.name != 'void':
        names.append('_retval')
    if len(names) == 0:
        return ''
    return ', '.join(names)

header = """/*
 * DO NOT EDIT.  THIS FILE IS GENERATED FROM %(filename)s
 */

#ifndef __gen_%(basename)s_h__
#define __gen_%(basename)s_h__
"""

include = """
#ifndef __gen_%(basename)s_h__
#include "%(basename)s.h"
#endif
"""

header_end = """/* For IDL files that don't want to include root IDL files. */
#ifndef NS_NO_VTABLE
#define NS_NO_VTABLE
#endif
"""

footer = """
#endif /* __gen_%(basename)s_h__ */
"""

forward_decl = """class %(name)s; /* forward declaration */

"""

def idl_basename(f):
    """returns the base name of a file with the last extension stripped"""
    return os.path.basename(f).rpartition('.')[0]

def print_header(idl, fd, filename):
    fd.write(header % {'filename': filename,
                       'basename': idl_basename(filename)})

    foundinc = False
    for inc in idl.includes():
        if not foundinc:
            foundinc = True
            fd.write('\n')
        fd.write(include % {'basename': idl_basename(inc.filename)})

    fd.write('\n')
    fd.write(header_end)

    for p in idl.productions:
        if p.kind == 'include': continue
        if p.kind == 'cdata':
            fd.write(p.data)
            continue

        if p.kind == 'forward':
            fd.write(forward_decl % {'name': p.name})
            continue
        if p.kind == 'interface':
            write_interface(p, fd)
            continue
        if p.kind == 'typedef':
            printComments(fd, p.doccomments, '')
            fd.write("typedef %s %s;\n\n" % (p.realtype.nativeType('in'),
                                             p.name))

    fd.write(footer % {'basename': idl_basename(filename)})

iface_header = r"""
/* starting interface:    %(name)s */
#define %(defname)s_IID_STR "%(iid)s"

#define %(defname)s_IID \
  {0x%(m0)s, 0x%(m1)s, 0x%(m2)s, \
    { %(m3joined)s }}

"""

uuid_decoder = re.compile(r"""(?P<m0>[a-f0-9]{8})-
                              (?P<m1>[a-f0-9]{4})-
                              (?P<m2>[a-f0-9]{4})-
                              (?P<m3>[a-f0-9]{4})-
                              (?P<m4>[a-f0-9]{12})$""", re.X)

iface_prolog = """ {
 public: 

  NS_DECLARE_STATIC_IID_ACCESSOR(%(defname)s_IID)

"""

iface_epilog = """};

  NS_DEFINE_STATIC_IID_ACCESSOR(%(name)s, %(defname)s_IID)

/* Use this macro when declaring classes that implement this interface. */
#define NS_DECL_%(macroname)s """


iface_forward = """

/* Use this macro to declare functions that forward the behavior of this interface to another object. */
#define NS_FORWARD_%(macroname)s(_to) """

iface_forward_safe = """

/* Use this macro to declare functions that forward the behavior of this interface to another object in a safe way. */
#define NS_FORWARD_SAFE_%(macroname)s(_to) """

iface_template_prolog = """

#if 0
/* Use the code below as a template for the implementation class for this interface. */

/* Header file */
class %(implclass)s : public %(name)s
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_%(macroname)s

  %(implclass)s();

private:
  ~%(implclass)s();

protected:
  /* additional members */
};

/* Implementation file */
NS_IMPL_ISUPPORTS1(%(implclass)s, %(name)s)

%(implclass)s::%(implclass)s()
{
  /* member initializers and constructor code */
}

%(implclass)s::~%(implclass)s()
{
  /* destructor code */
}

"""

example_tmpl = """%(returntype)s %(implclass)s::%(nativeName)s(%(paramList)s)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}
"""

iface_template_epilog = """/* End of implementation class template. */
#endif

"""

def write_interface(iface, fd):
    if iface.namemap is None:
        raise Exception("Interface was not resolved.")

    def write_const_decl(c):
        printComments(fd, c.doccomments, '  ')

        basetype = c.basetype
        value = c.getValue()

        fd.write("  enum { %(name)s = %(value)s%(signed)s };\n\n" % {
                     'name': c.name,
                     'value': value,
                     'signed': (not basetype.signed) and 'U' or ''})

    def write_method_decl(m):
        printComments(fd, m.doccomments, '  ')

        fd.write("  /* %s */\n" % m.toIDL())
        fd.write("  %s = 0;\n\n" % methodAsNative(m))
                                                                           
    def write_attr_decl(a):
        printComments(fd, a.doccomments, '  ')

        fd.write("  /* %s */\n" % a.toIDL());

        fd.write("  %s = 0;\n" % attributeAsNative(a, True))
        if not a.readonly:
            fd.write("  %s = 0;\n" % attributeAsNative(a, False))
        fd.write("\n")

    defname = iface.name.upper()
    if iface.name[0:2] == 'ns':
        defname = 'NS_' + defname[2:]

    names = uuid_decoder.match(iface.attributes.uuid).groupdict()
    m3str = names['m3'] + names['m4']
    names['m3joined'] = ", ".join(["0x%s" % m3str[i:i+2] for i in xrange(0, 16, 2)])

    if iface.name[2] == 'I':
        implclass = iface.name[:2] + iface.name[3:]
    else:
        implclass = '_MYCLASS_'

    names.update({'defname': defname,
                  'macroname': iface.name.upper(),
                  'name': iface.name,
                  'iid': iface.attributes.uuid,
                  'implclass': implclass})

    fd.write(iface_header % names)

    printComments(fd, iface.doccomments, '')

    fd.write("class ")
    foundcdata = False
    for m in iface.members:
        if isinstance(m, xpidl.CDATA):
            foundcdata = True

    if not foundcdata:
        fd.write("NS_NO_VTABLE ")

    if iface.attributes.scriptable:
        fd.write("NS_SCRIPTABLE ")
    if iface.attributes.deprecated:
        fd.write("NS_DEPRECATED ")
    fd.write(iface.name)
    if iface.base:
        fd.write(" : public %s" % iface.base)
    fd.write(iface_prolog % names)
    for member in iface.members:
        if isinstance(member, xpidl.ConstMember):
            write_const_decl(member)
        elif isinstance(member, xpidl.Attribute):
            write_attr_decl(member)
        elif isinstance(member, xpidl.Method):
            write_method_decl(member)
        elif isinstance(member, xpidl.CDATA):
            fd.write("  %s" % member.data)
        else:
            raise Exception("Unexpected interface member: %s" % member)

    fd.write(iface_epilog % names)

    for member in iface.members:
        if isinstance(member, xpidl.Attribute):
            fd.write("\\\n  %s; " % attributeAsNative(member, True))
            if not member.readonly:
                fd.write("\\\n  %s; " % attributeAsNative(member, False))
        elif isinstance(member, xpidl.Method):
            fd.write("\\\n  %s; " % methodAsNative(member))
    if len(iface.members) == 0:
        fd.write('\\\n  /* no methods! */')
    elif not member.kind in ('attribute', 'method'):
       fd.write('\\')

    fd.write(iface_forward % names)

    def emitTemplate(tmpl):
        for member in iface.members:
            if isinstance(member, xpidl.Attribute):
                fd.write(tmpl % {'asNative': attributeAsNative(member, True),
                                 'nativeName': attributeNativeName(member, True),
                                 'paramList': attributeParamName(member)})
                if not member.readonly:
                    fd.write(tmpl % {'asNative': attributeAsNative(member, False),
                                     'nativeName': attributeNativeName(member, False),
                                     'paramList': attributeParamName(member)})
            elif isinstance(member, xpidl.Method):
                fd.write(tmpl % {'asNative': methodAsNative(member),
                                 'nativeName': methodNativeName(member),
                                 'paramList': paramlistNames(member.params, member.realtype, member.notxpcom)})
        if len(iface.members) == 0:
            fd.write('\\\n  /* no methods! */')
        elif not member.kind in ('attribute', 'method'):
            fd.write('\\')

    emitTemplate("\\\n  %(asNative)s { return _to %(nativeName)s(%(paramList)s); } ")

    fd.write(iface_forward_safe % names)

    emitTemplate("\\\n  %(asNative)s { return !_to ? NS_ERROR_NULL_POINTER : _to->%(nativeName)s(%(paramList)s); } ")

    fd.write(iface_template_prolog % names)

    for member in iface.members:
        if isinstance(member, xpidl.ConstMember) or isinstance(member, xpidl.CDATA): continue
        fd.write("/* %s */\n" % member.toIDL())
        if isinstance(member, xpidl.Attribute):
            fd.write(example_tmpl % {'implclass': implclass,
                                     'returntype': 'NS_IMETHODIMP',
                                     'nativeName': attributeNativeName(member, True),
                                     'paramList': attributeParamlist(member, True)})
            if not member.readonly:
                fd.write(example_tmpl % {'implclass': implclass,
                                         'returntype': 'NS_IMETHODIMP',
                                         'nativeName': attributeNativeName(member, False),
                                         'paramList': attributeParamlist(member, False)})
        elif isinstance(member, xpidl.Method):
            fd.write(example_tmpl % {'implclass': implclass,
                                     'returntype': methodReturnType(member, 'NS_IMETHODIMP'),
                                     'nativeName': methodNativeName(member),
                                     'paramList': paramlistAsNative(member.params, member.realtype, notxpcom=member.notxpcom, empty='')})
        fd.write('\n')

    fd.write(iface_template_epilog)

if __name__ == '__main__':
    from optparse import OptionParser
    o = OptionParser()
    o.add_option('-I', action='append', dest='incdirs', help="Directory to search for imported files", default=[])
    o.add_option('--cachedir', dest='cachedir', help="Directory in which to cache lex/parse tables.", default='')
    options, args = o.parse_args()
    file, = args

    if options.cachedir != '':
        sys.path.append(options.cachedir)

    p = xpidl.IDLParser(outputdir=options.cachedir)
    idl = p.parse(open(file).read(), filename=file)
    idl.resolve(options.incdirs, p)
    print_header(idl, sys.stdout, file)
