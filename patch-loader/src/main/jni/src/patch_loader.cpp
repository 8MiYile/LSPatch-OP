/*
 * This file is part of LSPosed.
 *
 * LSPosed is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LSPosed is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LSPosed.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2022 LSPosed Contributors
 */

//
// Created by Nullptr on 2022/3/17.
//

#include "art/runtime/oat_file_manager.h"
#include "art/runtime/jit/profile_saver.h"
#include "elf_util.h"
#include "jni/bypass_sig.h"
#include "native_util.h"
#include "patch_loader.h"
#include "symbol_cache.h"
#include "utils/jni_helper.hpp"

using namespace lsplant;

namespace lspd {

    void PatchLoader::LoadDex(JNIEnv* env, Context::PreloadedDex&& dex) {
        auto mid_get_classloader = JNI_GetStaticMethodID(env, JNI_FindClass(env, "java/lang/ClassLoader"), "getSystemClassLoader", "()Ljava/lang/ClassLoader;");


        auto stub_classloader = JNI_CallStaticObjectMethod(env,JNI_FindClass(env, "java/lang/ClassLoader"),mid_get_classloader);

        if (!stub_classloader) [[unlikely]] {
            LOGE("getStubClassLoader failed!!!");
            return;
        }

        auto in_memory_classloader = JNI_FindClass(env, "dalvik/system/InMemoryDexClassLoader");
        auto mid_init = JNI_GetMethodID(env, in_memory_classloader, "<init>",
                                        "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");
        auto byte_buffer_class = JNI_FindClass(env, "java/nio/ByteBuffer");
        auto dex_buffer = env->NewDirectByteBuffer(dex.data(), dex.size());
        if (auto my_cl = JNI_NewObject(env, in_memory_classloader, mid_init, dex_buffer, stub_classloader)) {
            inject_class_loader_ = JNI_NewGlobalRef(env, my_cl);
        } else {
            LOGE("InMemoryDexClassLoader creation failed!!!");
            return;
        }

        env->DeleteLocalRef(dex_buffer);
    }

    void PatchLoader::InitArtHooker(JNIEnv* env, const InitInfo& initInfo) {
        Context::InitArtHooker(env, initInfo);
        handler = initInfo;
        art::DisableInline(initInfo);
        art::DisableBackgroundVerification(initInfo);
    }

    void PatchLoader::InitHooks(JNIEnv* env) {
        Context::InitHooks(env);
        RegisterBypass(env);
    }

    void PatchLoader::SetupEntryClass(JNIEnv* env) {
        if (auto entry_class = FindClassFromLoader(env, GetCurrentClassLoader(),
                                                   "org.lsposed.lspatch.loader.LSPApplication")) {
            entry_class_ = JNI_NewGlobalRef(env, entry_class);
        }
    }

    void PatchLoader::Load(JNIEnv* env) {
        lsplant::InitInfo initInfo {
                .inline_hooker = [](auto t, auto r) {
                    void* bk = nullptr;
                    return HookFunction(t, r, &bk) == RS_SUCCESS ? bk : nullptr;
                },
                .inline_unhooker = [](auto t) {
                    return UnhookFunction(t) == RT_SUCCESS;
                },
                .art_symbol_resolver = [](auto symbol) {
                    return GetArt()->getSymbAddress<void*>(symbol);
                },
                .art_symbol_prefix_resolver = [](auto symbol) {
                    return GetArt()->getSymbPrefixFirstAddress(symbol);
                },
        };

        auto stub = JNI_FindClass(env, "org/lsposed/lspatch/metaloader/LSPAppComponentFactoryStub");
        auto dex_field = JNI_GetStaticFieldID(env, stub, "dex", "[B");

        ScopedLocalRef<jbyteArray> array = JNI_GetStaticObjectField(env, stub, dex_field);
        auto dex = PreloadedDex {env->GetByteArrayElements(array.get(), nullptr), static_cast<size_t>(JNI_GetArrayLength(env, array))};

        InitArtHooker(env, initInfo);
        LoadDex(env, std::move(dex));
        InitHooks(env);

        GetArt(true);

        SetupEntryClass(env);
        FindAndCall(env, "onLoad", "()V");
    }
} // namespace lspd
