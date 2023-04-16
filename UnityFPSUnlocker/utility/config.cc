#include "config.hh"

#include <absl/status/status.h>
#include <dlfcn.h>
#include <jni.h>

#include <fstream>
#include <string>

#include "logger.hh"

using namespace rapidjson;

namespace Utility {
    absl::StatusOr<Document> LoadJsonFromFile(const char* FilePath) {
        Document doc;
        std::ifstream file(FilePath);
        if (file.is_open()) {
            std::string content((std::istreambuf_iterator<char>(file)),
                                (std::istreambuf_iterator<char>()));
            if (doc.Parse(content.c_str()).HasParseError()) {
                if (content.size() > 2) {
                    content[doc.GetErrorOffset() - 2] = '-';
                    content[doc.GetErrorOffset() - 1] = '>';
                }
                LOG("[%s] %s #%d\nFile: %s %s %zu %s", __FUNCTION__, __FILE__, __LINE__,
                    FilePath, GetParseError_En(doc.GetParseError()), doc.GetErrorOffset(), content.c_str());
                return absl::InternalError("Parse error");
            }
        }
        else {
            LOG("[%s] %s #%d\nCould not read file: %s.", __FUNCTION__, __FILE__, __LINE__, FilePath);
            return absl::InternalError("Could not read file.");
        }
        return doc;
    }

    jobject GetApplication(JNIEnv* env) {
        jclass activity_thread_clz = env->FindClass("android/app/ActivityThread");
        if (activity_thread_clz != nullptr) {
            jmethodID currentApplicationId = env->GetStaticMethodID(
                activity_thread_clz,
                "currentApplication",
                "()Landroid/app/Application;");
            if (currentApplicationId != nullptr) {
                return env->CallStaticObjectMethod(activity_thread_clz, currentApplicationId);
            }
            else {
                ERROR("Cannot find method: currentApplication() in ActivityThread.");
            }
        }
        else {
            ERROR("Cannot find class: android.app.ActivityThread");
        }
        return nullptr;
    }

    absl::StatusOr<jobject> GetApplicationInfo(JNIEnv* env) {
        jobject application = GetApplication(env);
        if (application == nullptr) {
            return absl::NotFoundError("No method id currentApplication");
        }
        jclass application_clazz = env->GetObjectClass(application);
        if (application_clazz == nullptr) {
            return absl::NotFoundError("No class application");
        }
        jmethodID get_application_info = env->GetMethodID(
            application_clazz,
            "getApplicationInfo",
            "()Landroid/content/pm/ApplicationInfo;");
        if (get_application_info) {
            return env->CallObjectMethod(application, get_application_info);
        }
        else {
            return absl::NotFoundError("No method id getApplicationInfo");
        }
    }

    absl::StatusOr<std::string> GetLibraryPath(JNIEnv* env, jobject application_info) {
        if (!application_info) {
            return absl::NotFoundError("No method id");
        }
        jfieldID native_library_dir_id = env->GetFieldID(
            env->GetObjectClass(application_info),
            "nativeLibraryDir",
            "Ljava/lang/String;");

        if (native_library_dir_id) {
            jstring native_library_dir_jstring = (jstring)env->GetObjectField(application_info, native_library_dir_id);
            const char* path = env->GetStringUTFChars(native_library_dir_jstring, 0);
            std::string package_name(path);
            env->ReleaseStringUTFChars(native_library_dir_jstring, path);
            return package_name;
        }
        else {
            return absl::NotFoundError("No nativeLibraryDir");
        }
    }

    absl::StatusOr<JavaVM*> GetVM(const char* art_lib) {
        // copy from https://github.com/frida/frida-core/blob/main/lib/agent/agent.vala
        void* art = dlopen(art_lib, RTLD_NOW);
        if (!art) {
            return absl::InternalError("Cannot open libart.so.");
        }

        using JNIGetCreatedJavaVMs_t = int (*)(JavaVM * *vmBuf, jsize bufLen, jsize * nVMs);
        static JNIGetCreatedJavaVMs_t JNIGetCreatedJavaVMsFunc = (JNIGetCreatedJavaVMs_t)dlsym(art, "JNI_GetCreatedJavaVMs");

        if (!JNIGetCreatedJavaVMsFunc) {
            return absl::NotFoundError("Cannot get symbol JNIGetCreatedJavaVMsFunc");
        }

        jsize numVMs;
        JavaVM* vms = nullptr;
        if (JNIGetCreatedJavaVMsFunc(&vms, 1, &numVMs) != 0) {
            return absl::NotFoundError("Cannot get vms");
        }
        return vms;
    }

    // Generated by ChatGPT.
    int ChangeMemPermission(void* p, size_t n, int permission) {

        void* page_aligned_p = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(p) & ~(::getpagesize() - 1));

        size_t protect_size = reinterpret_cast<uintptr_t>(p) + n - reinterpret_cast<uintptr_t>(page_aligned_p);
        if (protect_size % ::getpagesize() != 0) {
            protect_size += ::getpagesize();
        }

        return ::mprotect(page_aligned_p, protect_size, permission);
    }
} // namespace Utility