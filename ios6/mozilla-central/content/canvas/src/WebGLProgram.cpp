/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLContext.h"
#include "WebGLProgram.h"
#include "mozilla/dom/WebGLRenderingContextBinding.h"

using namespace mozilla;

/** Takes an ASCII string like "foo[i]", turns it into "foo" and returns "[i]" in bracketPart
  * 
  * \param string input/output: the string to split, becomes the string without the bracket part
  * \param bracketPart output: gets the bracket part.
  * 
  * Notice that if there are multiple brackets like "foo[i].bar[j]", only the last bracket is split.
  */
static bool SplitLastSquareBracket(nsACString& string, nsCString& bracketPart)
{
    MOZ_ASSERT(bracketPart.IsEmpty(), "SplitLastSquareBracket must be called with empty bracketPart string");

    if (string.IsEmpty())
        return false;

    char *string_start = string.BeginWriting();
    char *s = string_start + string.Length() - 1;

    if (*s != ']')
        return false;

    while (*s != '[' && s != string_start)
        s--;

    if (*s != '[')
        return false;

    bracketPart.Assign(s);
    *s = 0;
    string.EndWriting();
    string.SetLength(s - string_start);
    return true;
}

JSObject*
WebGLProgram::WrapObject(JSContext *cx, JSObject *scope, bool *triedToWrap) {
    return dom::WebGLProgramBinding::Wrap(cx, scope, this, triedToWrap);
}

WebGLProgram::WebGLProgram(WebGLContext *context)
    : WebGLContextBoundObject(context)
    , mLinkStatus(false)
    , mGeneration(0)
    , mAttribMaxNameLength(0)
{
    SetIsDOMBinding();
    mContext->MakeContextCurrent();
    mGLName = mContext->gl->fCreateProgram();
    mContext->mPrograms.insertBack(this);
}

void
WebGLProgram::Delete() {
    DetachShaders();
    mContext->MakeContextCurrent();
    mContext->gl->fDeleteProgram(mGLName);
    LinkedListElement<WebGLProgram>::removeFrom(mContext->mPrograms);
}

bool
WebGLProgram::AttachShader(WebGLShader *shader) {
    if (ContainsShader(shader))
        return false;
    mAttachedShaders.AppendElement(shader);

    mContext->MakeContextCurrent();
    mContext->gl->fAttachShader(GLName(), shader->GLName());

    return true;
}

bool
WebGLProgram::DetachShader(WebGLShader *shader) {
    if (!mAttachedShaders.RemoveElement(shader))
        return false;

    mContext->MakeContextCurrent();
    mContext->gl->fDetachShader(GLName(), shader->GLName());

    return true;
}

bool
WebGLProgram::HasAttachedShaderOfType(GLenum shaderType) {
    for (uint32_t i = 0; i < mAttachedShaders.Length(); ++i) {
        if (mAttachedShaders[i] && mAttachedShaders[i]->ShaderType() == shaderType) {
            return true;
        }
    }
    return false;
}

bool
WebGLProgram::HasBadShaderAttached() {
    for (uint32_t i = 0; i < mAttachedShaders.Length(); ++i) {
        if (mAttachedShaders[i] && !mAttachedShaders[i]->CompileStatus()) {
            return true;
        }
    }
    return false;
}

size_t
WebGLProgram::UpperBoundNumSamplerUniforms() {
    size_t numSamplerUniforms = 0;
    for (size_t i = 0; i < mAttachedShaders.Length(); ++i) {
        const WebGLShader *shader = mAttachedShaders[i];
        if (!shader)
            continue;
        for (size_t j = 0; j < shader->mUniformInfos.Length(); ++j) {
            WebGLUniformInfo u = shader->mUniformInfos[j];
            if (u.type == SH_SAMPLER_2D ||
                u.type == SH_SAMPLER_CUBE)
            {
                numSamplerUniforms += u.arraySize;
            }
        }
    }
    return numSamplerUniforms;
}

void
WebGLProgram::MapIdentifier(const nsACString& name, nsCString *mappedName) {
    if (!mIdentifierMap) {
        // if the identifier map doesn't exist yet, build it now
        mIdentifierMap = new CStringMap;
        mIdentifierMap->Init();
        for (size_t i = 0; i < mAttachedShaders.Length(); i++) {
            for (size_t j = 0; j < mAttachedShaders[i]->mAttributes.Length(); j++) {
                const WebGLMappedIdentifier& attrib = mAttachedShaders[i]->mAttributes[j];
                mIdentifierMap->Put(attrib.original, attrib.mapped);
            }
            for (size_t j = 0; j < mAttachedShaders[i]->mUniforms.Length(); j++) {
                const WebGLMappedIdentifier& uniform = mAttachedShaders[i]->mUniforms[j];
                mIdentifierMap->Put(uniform.original, uniform.mapped);
            }
        }
    }

    nsCString mutableName(name);
    nsCString bracketPart;
    bool hadBracketPart = SplitLastSquareBracket(mutableName, bracketPart);
    if (hadBracketPart)
        mutableName.AppendLiteral("[0]");

    if (mIdentifierMap->Get(mutableName, mappedName)) {
        if (hadBracketPart) {
            nsCString mappedBracketPart;
            bool mappedHadBracketPart = SplitLastSquareBracket(*mappedName, mappedBracketPart);
            if (mappedHadBracketPart)
                mappedName->Append(bracketPart);
        }
        return;
    }

    // not found? We might be in the situation we have a uniform array name and the GL's glGetActiveUniform
    // returned its name without [0], as is allowed by desktop GL but not in ES. Let's then try with [0].
    mutableName.AppendLiteral("[0]");
    if (mIdentifierMap->Get(mutableName, mappedName))
        return;

    // not found? return name unchanged. This case happens e.g. on bad user input, or when
    // we're not using identifier mapping, or if we didn't store an identifier in the map because
    // e.g. its mapping is trivial (as happens for short identifiers)
    mappedName->Assign(name);
}

void
WebGLProgram::ReverseMapIdentifier(const nsACString& name, nsCString *reverseMappedName) {
    if (!mIdentifierReverseMap) {
        // if the identifier reverse map doesn't exist yet, build it now
        mIdentifierReverseMap = new CStringMap;
        mIdentifierReverseMap->Init();
        for (size_t i = 0; i < mAttachedShaders.Length(); i++) {
            for (size_t j = 0; j < mAttachedShaders[i]->mAttributes.Length(); j++) {
                const WebGLMappedIdentifier& attrib = mAttachedShaders[i]->mAttributes[j];
                mIdentifierReverseMap->Put(attrib.mapped, attrib.original);
            }
            for (size_t j = 0; j < mAttachedShaders[i]->mUniforms.Length(); j++) {
                const WebGLMappedIdentifier& uniform = mAttachedShaders[i]->mUniforms[j];
                mIdentifierReverseMap->Put(uniform.mapped, uniform.original);
            }
        }
    }

    nsCString mutableName(name);
    nsCString bracketPart;
    bool hadBracketPart = SplitLastSquareBracket(mutableName, bracketPart);
    if (hadBracketPart)
        mutableName.AppendLiteral("[0]");

    if (mIdentifierReverseMap->Get(mutableName, reverseMappedName)) {
        if (hadBracketPart) {
            nsCString reverseMappedBracketPart;
            bool reverseMappedHadBracketPart = SplitLastSquareBracket(*reverseMappedName, reverseMappedBracketPart);
            if (reverseMappedHadBracketPart)
                reverseMappedName->Append(bracketPart);
        }
        return;
    }

    // not found? We might be in the situation we have a uniform array name and the GL's glGetActiveUniform
    // returned its name without [0], as is allowed by desktop GL but not in ES. Let's then try with [0].
    mutableName.AppendLiteral("[0]");
    if (mIdentifierReverseMap->Get(mutableName, reverseMappedName))
        return;

    // not found? return name unchanged. This case happens e.g. on bad user input, or when
    // we're not using identifier mapping, or if we didn't store an identifier in the map because
    // e.g. its mapping is trivial (as happens for short identifiers)
    reverseMappedName->Assign(name);
}

WebGLUniformInfo
WebGLProgram::GetUniformInfoForMappedIdentifier(const nsACString& name) {
    if (!mUniformInfoMap) {
        // if the identifier-to-array-size map doesn't exist yet, build it now
        mUniformInfoMap = new CStringToUniformInfoMap;
        mUniformInfoMap->Init();
        for (size_t i = 0; i < mAttachedShaders.Length(); i++) {
            for (size_t j = 0; j < mAttachedShaders[i]->mUniforms.Length(); j++) {
                const WebGLMappedIdentifier& uniform = mAttachedShaders[i]->mUniforms[j];
                const WebGLUniformInfo& info = mAttachedShaders[i]->mUniformInfos[j];
                mUniformInfoMap->Put(uniform.mapped, info);
            }
        }
    }

    nsCString mutableName(name);
    nsCString bracketPart;
    bool hadBracketPart = SplitLastSquareBracket(mutableName, bracketPart);
    // if there is a bracket, we're either an array or an entry in an array.
    if (hadBracketPart)
        mutableName.AppendLiteral("[0]");

    WebGLUniformInfo info;
    mUniformInfoMap->Get(mutableName, &info);
    // we don't check if that Get failed, as if it did, it left info with default values

    // if there is a bracket and it's not [0], then we're not an array, we're just an entry in an array
    if (hadBracketPart && !bracketPart.EqualsLiteral("[0]")) {
        info.isArray = false;
        info.arraySize = 1;
    }
    return info;
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_1(WebGLProgram, mAttachedShaders)

NS_IMPL_CYCLE_COLLECTING_ADDREF(WebGLProgram)
NS_IMPL_CYCLE_COLLECTING_RELEASE(WebGLProgram)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(WebGLProgram)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END
