#ifndef NAPI_CANON_API_PROPERTY_SHUTTER_SPEED_H
#define NAPI_CANON_API_PROPERTY_SHUTTER_SPEED_H

#include "types.h"

namespace CameraApi {

    class ShutterSpeed : public Napi::ObjectWrap<ShutterSpeed> {
        public:
            explicit ShutterSpeed(const Napi::CallbackInfo &info);

            static void Init(Napi::Env env, Napi::Object exports);

            static Napi::Object NewInstance(Napi::Env env, EdsInt32 value);

        private:
            static constexpr const char JSClassName[] = "ShutterSpeed";

            static inline Napi::Function JSConstructor(Napi::Function *func = nullptr) {
                static Napi::FunctionReference constructor;

                if (func != nullptr) {
                    constructor = Napi::Persistent(*func);
                    constructor.SuppressDestruct();
                }
                return constructor.Value();
            }

            EdsInt32 value_ = 0;
            double seconds_ = 0;
            ExposureStopSize stopSize_ = ExposureStopSize::OneHalf;

            static std::string GetLabelForValue(EdsInt32 value);

            static std::string GetLabelForSeconds(
                double seconds, ExposureStopSize stopSize = ExposureStopSize::OneHalf
            );

            Napi::Value GetLabel(const Napi::CallbackInfo &info);

            Napi::Value GetValue(const Napi::CallbackInfo &info);

            Napi::Value GetSeconds(const Napi::CallbackInfo &info);

            Napi::Value GetStop(const Napi::CallbackInfo &info);

            Napi::Value GetPrimitive(const Napi::CallbackInfo &info);

            Napi::Value ToJSON(const Napi::CallbackInfo &info);

            Napi::Value ToStringTag(const Napi::CallbackInfo &info);

            Napi::Value Inspect(const Napi::CallbackInfo &info);

            static EdsInt32 ForLabel(const std::string& label);

            static Napi::Value ForLabel(const Napi::CallbackInfo &info);

            static Napi::Value FindNearest(const Napi::CallbackInfo &info);
    };
}

#endif //NAPI_CANON_API_PROPERTY_SHUTTER_SPEED_H
