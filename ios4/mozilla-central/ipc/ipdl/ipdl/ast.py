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
# Contributor(s):
#   Chris Jones <jones.chris.g@gmail.com>
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

import sys

class Visitor:
    def defaultVisit(self, node):
        raise Exception, "INTERNAL ERROR: no visitor for node type `%s'"% (
            node.__class__.__name__)

    def visitTranslationUnit(self, tu):
        for cxxInc in tu.cxxIncludes:
            cxxInc.accept(self)
        for protoInc in tu.protocolIncludes:
            protoInc.accept(self)
        for su in tu.structsAndUnions:
            su.accept(self)
        for using in tu.using:
            using.accept(self)
        tu.protocol.accept(self)

    def visitCxxInclude(self, inc):
        pass

    def visitProtocolInclude(self, inc):
        # Note: we don't visit the child AST here, because that needs delicate
        # and pass-specific handling
        pass

    def visitStructDecl(self, struct):
        for f in struct.fields:
            f.accept(self)

    def visitStructField(self, field):
        field.type.accept(self)

    def visitUnionDecl(self, union):
        for t in union.components:
            t.accept(self)

    def visitUsingStmt(self, using):
        pass

    def visitProtocol(self, p):
        for namespace in p.namespaces:
            namespace.accept(self)
        for spawns in p.spawnsStmts:
            spawns.accept(self)
        for bridges in p.bridgesStmts:
            bridges.accept(self)
        for mgr in p.managers:
            mgr.accept(self)
        for managed in p.managesStmts:
            managed.accept(self)
        for msgDecl in p.messageDecls:
            msgDecl.accept(self)
        for transitionStmt in p.transitionStmts:
            transitionStmt.accept(self)

    def visitNamespace(self, ns):
        pass

    def visitSpawnsStmt(self, spawns):
        pass

    def visitBridgesStmt(self, bridges):
        pass

    def visitManager(self, mgr):
        pass

    def visitManagesStmt(self, mgs):
        pass

    def visitMessageDecl(self, md):
        for inParam in md.inParams:
            inParam.accept(self)
        for outParam in md.outParams:
            outParam.accept(self)

    def visitTransitionStmt(self, ts):
        ts.state.accept(self)
        for trans in ts.transitions:
            trans.accept(self)

    def visitTransition(self, t):
        for toState in t.toStates:
            toState.accept(self)

    def visitState(self, s):
        pass

    def visitParam(self, decl):
        pass

    def visitTypeSpec(self, ts):
        pass

    def visitDecl(self, d):
        pass

class Loc:
    def __init__(self, filename='<??>', lineno=0):
        assert filename
        self.filename = filename
        self.lineno = lineno
    def __repr__(self):
        return '%r:%r'% (self.filename, self.lineno)
    def __str__(self):
        return '%s:%s'% (self.filename, self.lineno)

Loc.NONE = Loc(filename='<??>', lineno=0)

class _struct:
    pass

class Node:
    def __init__(self, loc=Loc.NONE):
        self.loc = loc

    def accept(self, visitor):
        visit = getattr(visitor, 'visit'+ self.__class__.__name__, None)
        if visit is None:
            return getattr(visitor, 'defaultVisit')(self)
        return visit(self)

    def addAttrs(self, attrsName):
        if not hasattr(self, attrsName):
            setattr(self, attrsName, _struct())


class NamespacedNode(Node):
    def __init__(self, loc=Loc.NONE, name=None):
        Node.__init__(self, loc)
        self.name = name
        self.namespaces = [ ]  

    def addOuterNamespace(self, namespace):
        self.namespaces.insert(0, namespace)

    def qname(self):
        return QualifiedId(self.loc, self.name,
                           [ ns.name for ns in self.namespaces ])

class TranslationUnit(Node):
    def __init__(self):
        Node.__init__(self)
        self.filename = None
        self.cxxIncludes = [ ]
        self.protocolIncludes = [ ]
        self.using = [ ]
        self.structsAndUnions = [ ]
        self.protocol = None

    def addCxxInclude(self, cxxInclude): self.cxxIncludes.append(cxxInclude)
    def addProtocolInclude(self, pInc): self.protocolIncludes.append(pInc)
    def addStructDecl(self, struct): self.structsAndUnions.append(struct)
    def addUnionDecl(self, union): self.structsAndUnions.append(union)
    def addUsingStmt(self, using): self.using.append(using)

    def setProtocol(self, protocol): self.protocol = protocol

class CxxInclude(Node):
    def __init__(self, loc, cxxFile):
        Node.__init__(self, loc)
        self.file = cxxFile

class ProtocolInclude(Node):
    def __init__(self, loc, protocolName):
        Node.__init__(self, loc)
        self.file = "%s.ipdl" % protocolName

class UsingStmt(Node):
    def __init__(self, loc, cxxTypeSpec):
        Node.__init__(self, loc)
        self.type = cxxTypeSpec

# "singletons"
class ASYNC:
    pretty = 'async'
    @classmethod
    def __hash__(cls): return hash(cls.pretty)
    @classmethod
    def __str__(cls):  return cls.pretty
class RPC:
    pretty = 'rpc'
    @classmethod
    def __hash__(cls): return hash(cls.pretty)
    @classmethod
    def __str__(cls):  return cls.pretty
class SYNC:
    pretty = 'sync'
    @classmethod
    def __hash__(cls): return hash(cls.pretty)
    @classmethod
    def __str__(cls):  return cls.pretty

class INOUT:
    pretty = 'inout'
    @classmethod
    def __hash__(cls): return hash(cls.pretty)
    @classmethod
    def __str__(cls):  return cls.pretty
class IN:
    pretty = 'in'
    @classmethod
    def __hash__(cls): return hash(cls.pretty)
    @classmethod
    def __str__(cls):  return cls.pretty
    @staticmethod
    def prettySS(cls, ss): return _prettyTable['in'][ss.pretty]
class OUT:
    pretty = 'out'
    @classmethod
    def __hash__(cls): return hash(cls.pretty)
    @classmethod
    def __str__(cls):  return cls.pretty
    @staticmethod
    def prettySS(ss): return _prettyTable['out'][ss.pretty]

_prettyTable = {
    IN  : { 'async': 'AsyncRecv',
            'sync': 'SyncRecv',
            'rpc': 'RpcAnswer' },
    OUT : { 'async': 'AsyncSend',
            'sync': 'SyncSend',
            'rpc': 'RpcCall' }
    # inout doesn't make sense here
}


class Namespace(Node):
    def __init__(self, loc, namespace):
        Node.__init__(self, loc)
        self.name = namespace

class Protocol(NamespacedNode):
    def __init__(self, loc):
        NamespacedNode.__init__(self, loc)
        self.sendSemantics = ASYNC
        self.spawnsStmts = [ ]
        self.bridgesStmts = [ ]
        self.managers = [ ]
        self.managesStmts = [ ]
        self.messageDecls = [ ]
        self.transitionStmts = [ ]
        self.startStates = [ ]

class StructField(Node):
    def __init__(self, loc, type, name):
        Node.__init__(self, loc)
        self.type = type
        self.name = name

class StructDecl(NamespacedNode):
    def __init__(self, loc, name, fields):
        NamespacedNode.__init__(self, loc, name)
        self.fields = fields

class UnionDecl(NamespacedNode):
    def __init__(self, loc, name, components):
        NamespacedNode.__init__(self, loc, name)
        self.components = components

class SpawnsStmt(Node):
    def __init__(self, loc, side, proto, spawnedAs):
        Node.__init__(self, loc)
        self.side = side
        self.proto = proto
        self.spawnedAs = spawnedAs

class BridgesStmt(Node):
    def __init__(self, loc, parentSide, childSide):
        Node.__init__(self, loc)
        self.parentSide = parentSide
        self.childSide = childSide

class Manager(Node):
    def __init__(self, loc, managerName):
        Node.__init__(self, loc)
        self.name = managerName

class ManagesStmt(Node):
    def __init__(self, loc, managedName):
        Node.__init__(self, loc)
        self.name = managedName

class MessageDecl(Node):
    def __init__(self, loc):
        Node.__init__(self, loc)
        self.name = None
        self.sendSemantics = ASYNC
        self.direction = None
        self.inParams = [ ]
        self.outParams = [ ]

    def addInParams(self, inParamsList):
        self.inParams += inParamsList

    def addOutParams(self, outParamsList):
        self.outParams += outParamsList

    def hasReply(self):
        return self.sendSemantics is SYNC or self.sendSemantics is RPC

class Transition(Node):
    def __init__(self, loc, trigger, msg, toStates):
        Node.__init__(self, loc)
        self.trigger = trigger
        self.msg = msg
        self.toStates = toStates

    def __cmp__(self, o):
        c = cmp(self.msg, o.msg)
        if c: return c
        c = cmp(self.trigger, o.trigger)
        if c: return c

    def __hash__(self): return hash(str(self))
    def __str__(self): return '%s %s'% (self.trigger, self.msg)

    @staticmethod
    def nameToTrigger(name):
        return { 'send': SEND, 'recv': RECV, 'call': CALL, 'answer': ANSWER }[name]

Transition.NULL = Transition(Loc.NONE, None, None, [ ])

class TransitionStmt(Node):
    def __init__(self, loc, state, transitions):
        Node.__init__(self, loc)
        self.state = state
        self.transitions = transitions

    @staticmethod
    def makeNullStmt(state):
        return TransitionStmt(Loc.NONE, state, [ Transition.NULL ])

class SEND:
    pretty = 'send'
    @classmethod
    def __hash__(cls): return hash(cls.pretty)
    @classmethod
    def direction(cls): return OUT
class RECV:
    pretty = 'recv'
    @classmethod
    def __hash__(cls): return hash(cls.pretty)
    @classmethod
    def direction(cls): return IN
class CALL:
    pretty = 'call'
    @classmethod
    def __hash__(cls): return hash(cls.pretty)
    @classmethod
    def direction(cls): return OUT
class ANSWER:
    pretty = 'answer'
    @classmethod
    def __hash__(cls): return hash(cls.pretty)
    @classmethod
    def direction(cls): return IN

class State(Node):
    def __init__(self, loc, name, start=False):
        Node.__init__(self, loc)
        self.name = name
        self.start = start
    def __eq__(self, o):
         return (isinstance(o, State)
                 and o.name == self.name
                 and o.start == self.start)
    def __hash__(self):
        return hash(repr(self))
    def __ne__(self, o):
        return not (self == o)
    def __repr__(self): return '<State %r start=%r>'% (self.name, self.start)
    def __str__(self): return '<State %s start=%s>'% (self.name, self.start)

State.ANY = State(Loc.NONE, '[any]', start=True)
State.DEAD = State(Loc.NONE, '[dead]', start=False)

class Param(Node):
    def __init__(self, loc, typespec, name):
        Node.__init__(self, loc)
        self.name = name
        self.typespec = typespec

class TypeSpec(Node):
    def __init__(self, loc, spec, state=None, array=0, nullable=0,
                 myChmod=None, otherChmod=None):
        Node.__init__(self, loc)
        self.spec = spec                # QualifiedId
        self.state = state              # None or State
        self.array = array              # bool
        self.nullable = nullable        # bool
        self.myChmod = myChmod          # None or string
        self.otherChmod = otherChmod    # None or string

    def basename(self):
        return self.spec.baseid

    def isActor(self):
        return self.state is not None

    def __str__(self):  return str(self.spec)

class QualifiedId:              # FIXME inherit from node?
    def __init__(self, loc, baseid, quals=[ ]):
        assert isinstance(baseid, str)
        for qual in quals: assert isinstance(qual, str)

        self.loc = loc
        self.baseid = baseid
        self.quals = quals

    def qualify(self, id):
        self.quals.append(self.baseid)
        self.baseid = id

    def __str__(self):
        if 0 == len(self.quals):
            return self.baseid
        return '::'.join(self.quals) +'::'+ self.baseid

# added by type checking passes
class Decl(Node):
    def __init__(self, loc):
        Node.__init__(self, loc)
        self.progname = None    # what the programmer typed, if relevant
        self.shortname = None   # shortest way to refer to this decl
        self.fullname = None    # full way to refer to this decl
        self.loc = loc
        self.type = None
        self.scope = None
