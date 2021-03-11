#include "camera.h"
#include "camera-browser.h"
#include "camera-file.h"
#include "property-option.h"
#include "camera-property.h"
#include "state-event.h"
#include "object-event.h"
#include "api-error.h"
#include "base64.h"
#include "utility.h"
#include <iostream>
#include <napi.h>

namespace CameraApi {

    struct LiveViewEventData {
        CameraReference camera;
        boolean isActive = false;
    };

    struct PropertyEventData {
        CameraReference camera;
        EdsPropertyEvent eventID = 0;
        EdsUInt32 propertyID = 0;
        EdsUInt32 specifier = 0;
    };

    struct ObjectEventData {
        CameraReference camera;
        EdsObjectEvent eventID = 0;
        EdsBaseRef objectRef = nullptr;
    };

    struct StateEventData {
        CameraReference camera;
        EdsStateEvent eventID = 0;
    };

    LabelMap CameraCommands = {
        {kEdsCameraCommand_TakePicture, "TakePicture"},
        {kEdsCameraCommand_ExtendShutDownTimer, "ExtendShutDownTimer"},
        {kEdsCameraCommand_BulbStart, "BulbStart"},
        {kEdsCameraCommand_BulbEnd, "BulbEnd"},
        {kEdsCameraCommand_DoEvfAf, "DoEvfAf"},
        {kEdsCameraCommand_DriveLensEvf, "DriveLensEvf"},
        {kEdsCameraCommand_DoClickWBEvf, "DoClickWBEvf"},
        {kEdsCameraCommand_MovieSelectSwON, "MovieSelectSwON"},
        {kEdsCameraCommand_MovieSelectSwOFF, "MovieSelectSwOFF"},
        {kEdsCameraCommand_PressShutterButton, "PressShutterButton"},
        {kEdsCameraCommand_RequestRollPitchLevel, "RequestRollPitchLevel"},
        {kEdsCameraCommand_DrivePowerZoom, "DrivePowerZoom"},
        {kEdsCameraCommand_SetRemoteShootingMode, "SetRemoteShootingMode"}
    };

    LabelMap CameraParametersShutterButton = {
        {kEdsCameraCommand_ShutterButton_OFF, "OFF"},
        {kEdsCameraCommand_ShutterButton_Halfway, "Halfway"},
        {kEdsCameraCommand_ShutterButton_Completely, "Completely"},
        {kEdsCameraCommand_ShutterButton_Halfway_NonAF, "HalfwayNonAF"},
        {kEdsCameraCommand_ShutterButton_Completely_NonAF, "CompletelyNonAF"}
    };

    Camera::Camera(const EdsCameraRef edsCamera) {

        if (!edsCamera) {
            throw std::runtime_error("Error - Invalid camera reference.");
        }

        EdsRetain(edsCamera);

        edsCamera_ = edsCamera;
        EdsError error = EDS_ERR_OK;
        error = EdsGetDeviceInfo(edsCamera_, &deviceInfo_);
        if (error != EDS_ERR_OK) {
            throw std::runtime_error("ERROR - failed to get device info");
        }

        error = EdsSetCameraStateEventHandler(edsCamera_, kEdsStateEvent_All, Camera::handleStateEvent, this);
        if (error != EDS_ERR_OK) {
            throw std::runtime_error("ERROR - failed to set state event handler");
        }
        error = EdsSetPropertyEventHandler(edsCamera_, kEdsPropertyEvent_All, Camera::handlePropertyEvent, this);
        if (error != EDS_ERR_OK) {
            throw std::runtime_error("ERROR - failed to set property event handler");
        }
        error = EdsSetObjectEventHandler(edsCamera_, kEdsObjectEvent_All, Camera::handleObjectEvent, this);
        if (error != EDS_ERR_OK) {
            throw std::runtime_error("ERROR - failed to set object event handler");
        }
    }

    Camera::~Camera() {
        if (isConnected_) {
            disconnect();
        }
        EdsSetCameraStateEventHandler(edsCamera_, kEdsStateEvent_All, nullptr, this);
        EdsSetPropertyEventHandler(edsCamera_, kEdsStateEvent_All, nullptr, this);
        EdsSetObjectEventHandler(edsCamera_, kEdsStateEvent_All, nullptr, this);
        EdsRelease(edsCamera_);
        edsCamera_ = nullptr;
    }

    Napi::ThreadSafeFunction &Camera::getEventEmit() {
        return CameraBrowser::instance()->getEventEmit();
    }

    bool Camera::hasEventEmit() {
        return CameraBrowser::instance()->hasEventEmit();
    }

    CameraReference Camera::create(const EdsCameraRef &edsCamera) {
        return CameraReference(new Camera(edsCamera))->shared_from_this();
    }

    EdsError Camera::connect(bool shouldKeepAlive = false) {
        shouldKeepAlive_ = shouldKeepAlive;
        if (isConnected_) {
            return EDS_ERR_OK;
        }

        EdsUInt32 id;
        id = kEdsPropID_UTCTime;
        EdsSetPropertyData(edsCamera_, 0x01000000, 0x51DD2696, sizeof(id), &id);
        id = kEdsPropID_TimeZone;
        EdsSetPropertyData(edsCamera_, 0x01000000, 0xFA71F7, sizeof(id), &id);
        id = kEdsPropID_SummerTimeSetting;
        EdsSetPropertyData(edsCamera_, 0x01000000, 0x9780670, sizeof(id), &id);

        EdsError error = EdsOpenSession(edsCamera_);
        if (error != EDS_ERR_OK) {
            return error;
        }
        isConnected_ = true;
        return EDS_ERR_OK;
    }

    EdsError Camera::disconnect() {
        if (!isConnected_) {
            return EDS_ERR_SESSION_NOT_OPEN;
        }
        stopLiveView();
        isConnected_ = false;
        return EdsCloseSession(edsCamera_);
    }

    EdsError Camera::sendCommand(EdsCameraCommand command, EdsInt32 parameter) {
        if (!isConnected_) {
            return EDS_ERR_SESSION_NOT_OPEN;
        }
        return EdsSendCommand(edsCamera_, command, parameter);
    }

    EdsError Camera::takePicture() {
        if (!isConnected_) {
            return EDS_ERR_SESSION_NOT_OPEN;
        }
        EdsError error;
        error = EdsSendCommand(
            edsCamera_, kEdsCameraCommand_PressShutterButton, kEdsCameraCommand_ShutterButton_Halfway
        );
        if (EDS_ERR_OK == error) {
            error = EdsSendCommand(
                edsCamera_, kEdsCameraCommand_PressShutterButton, kEdsCameraCommand_ShutterButton_Completely
            );
        }
        EdsSendCommand(
            edsCamera_, kEdsCameraCommand_PressShutterButton, kEdsCameraCommand_ShutterButton_OFF
        );
        return error;
    }

    EdsError Camera::startLiveView() {
        if (!isConnected_) {
            return EDS_ERR_SESSION_NOT_OPEN;
        }
        EdsError error;
        EdsUInt32 device;
        error = EdsGetPropertyData(edsCamera_, kEdsPropID_Evf_OutputDevice, 0, sizeof(device), &device);
        if (error == EDS_ERR_OK) {
            device |= kEdsEvfOutputDevice_PC;
            error = EdsSetPropertyData(edsCamera_, kEdsPropID_Evf_OutputDevice, 0, sizeof(device), &device);
        }
        return error;
    }

    EdsError Camera::stopLiveView() {
        if (!isConnected_) {
            return EDS_ERR_SESSION_NOT_OPEN;
        }
        EdsError error = EDS_ERR_OK;
        EdsUInt32 device;
        error = EdsGetPropertyData(edsCamera_, kEdsPropID_Evf_OutputDevice, 0, sizeof(device), &device);
        if (error == EDS_ERR_OK) {
            device &= ~kEdsEvfOutputDevice_PC;
            error = EdsSetPropertyData(edsCamera_, kEdsPropID_Evf_OutputDevice, 0, sizeof(device), &device);
        }
        return error;
    }

    bool Camera::updateLiveViewStatus() {
        EdsError error;
        EdsUInt32 device;
        error = EdsGetPropertyData(edsCamera_, kEdsPropID_Evf_OutputDevice, 0, sizeof(device), &device);
        bool activeLiveView = ((device & kEdsEvfOutputDevice_PC) == kEdsEvfOutputDevice_PC);
        if (hasActiveLiveView_ != activeLiveView) {
            hasActiveLiveView_ = activeLiveView;

            LiveViewEventData *eventDataPtr;
            eventDataPtr = new LiveViewEventData;
            eventDataPtr->camera = this->shared_from_this();
            eventDataPtr->isActive = activeLiveView;

            if (hasEventEmit()) {
                getEventEmit().BlockingCall(
                    eventDataPtr, [](Napi::Env env, Napi::Function jsCallback, LiveViewEventData *dataPtr) {
                        Napi::Object event = Napi::Object::New(env);
                        event.Set("camera", CameraWrap::NewInstance(env, dataPtr->camera));
                        jsCallback.Call(
                            {
                                Napi::String::New(
                                    env, dataPtr->isActive ? EventName_LiveViewStart : EventName_LiveViewStop
                                ),
                                event
                            }
                        );
                        delete dataPtr;
                    }
                );
            }
        }
        return hasActiveLiveView_;
    }

    EdsError Camera::downloadLiveViewImage(std::string &image) {
        if (!hasActiveLiveView_) {
            return EDS_ERR_OK;
        }
        EdsError error = EDS_ERR_OK;
        EdsStreamRef stream = nullptr;
        EdsEvfImageRef evfImage = nullptr;

        error = EdsCreateMemoryStream(0, &stream);
        if (error == EDS_ERR_OK) {
            error = EdsCreateEvfImageRef(stream, &evfImage);
        }
        if (error == EDS_ERR_OK) {
            error = EdsDownloadEvfImage(edsCamera_, evfImage);
        }
        if (error == EDS_ERR_OK) {
            EdsUInt64 imageDataLength;
            int imageStringLength;
            unsigned char *imageData;

            EdsGetLength(stream, &imageDataLength);
            if (imageDataLength > 0) {
                EdsGetPointer(stream, (EdsVoid **) &imageData);

                char *imageString = base64(imageData, imageDataLength, &imageStringLength);
                image.assign(imageString);
                free(imageString);
            }
        }
        if (stream != nullptr) {
            EdsRelease(stream);
            stream = nullptr;
        }
        if (stream != nullptr) {
            EdsRelease(evfImage);
            evfImage = nullptr;
        }
        return error;
    }

    EdsError __stdcall Camera::handleStateEvent(EdsStateEvent inEvent, EdsUInt32 inEventData, EdsVoid *inContext) {
        auto *c = (Camera *) inContext;
        CameraReference camera = c->shared_from_this();

        switch (inEvent) {
            case kEdsStateEvent_WillSoonShutDown:
                if (camera->isConnected_ && camera->shouldKeepAlive_) {
                    EdsSendCommand(camera->edsCamera_, kEdsCameraCommand_ExtendShutDownTimer, 0);
                }
                break;
            case kEdsStateEvent_Shutdown:
                camera->disconnect();
                CameraBrowser::instance()->removeCamera(camera);
                break;
            default:
                StateEventData *eventDataPtr;
                eventDataPtr = new StateEventData;
                eventDataPtr->camera = camera;
                eventDataPtr->eventID = inEvent;

                if (camera->hasEventEmit()) {
                    camera->getEventEmit().BlockingCall(
                        eventDataPtr, [](Napi::Env env, Napi::Function jsCallback, StateEventData *dataPtr) {
                            Napi::Object event = Napi::Object::New(env);
                            event.Set("camera", CameraWrap::NewInstance(env, dataPtr->camera));
                            event.Set("stateEvent", StateEvent::NewInstance(env, dataPtr->eventID));
                            jsCallback.Call(
                                {
                                    Napi::String::New(env, EventName_StateChange),
                                    event
                                }
                            );
                            delete dataPtr;
                        }
                    );
                }
                break;
        }
        return EDS_ERR_OK;
    }

    EdsError __stdcall Camera::handlePropertyEvent(
        EdsPropertyEvent inEvent, EdsUInt32 inPropertyID, EdsUInt32 inParam, EdsVoid *inContext
    ) {
        auto *c = (Camera *) inContext;
        CameraReference camera = c->shared_from_this();

        PropertyEventData *eventDataPtr;
        eventDataPtr = new PropertyEventData;
        eventDataPtr->camera = camera;
        eventDataPtr->eventID = inEvent;
        eventDataPtr->propertyID = inPropertyID;
        eventDataPtr->specifier = inParam;

        switch (inPropertyID) {
            case kEdsPropID_Evf_OutputDevice:
                camera->updateLiveViewStatus();
                break;
        }

        if (camera->hasEventEmit()) {
            camera->getEventEmit().BlockingCall(
                eventDataPtr,
                [](Napi::Env env, Napi::Function jsCallback, PropertyEventData *dataPtr) {
                    Napi::Object event = Napi::Object::New(env);
                    event.Set("camera", CameraWrap::NewInstance(env, dataPtr->camera));
                    event.Set(
                        "property",
                        CameraProperty::NewInstance(
                            env,
                            dataPtr->camera,
                            dataPtr->propertyID,
                            dataPtr->specifier
                        )
                    );
                    jsCallback.Call(
                        {
                            Napi::String::New(
                                env,
                                dataPtr->eventID == kEdsPropertyEvent_PropertyDescChanged
                                ? EventName_PropertyChangeOptions
                                : EventName_PropertyChangeValue
                            ),
                            event
                        }
                    );
                    delete dataPtr;
                }
            );
        }

        return EDS_ERR_OK;
    }

    EdsError __stdcall Camera::handleObjectEvent(EdsObjectEvent inEvent, EdsBaseRef inRef, EdsVoid *inContext) {
        auto *c = (Camera *) inContext;
        CameraReference camera = c->shared_from_this();

        ObjectEventData *eventDataPtr;
        eventDataPtr = new ObjectEventData;
        eventDataPtr->camera = camera;
        eventDataPtr->eventID = inEvent;
        eventDataPtr->objectRef = inRef;

        try {
            if (camera->hasEventEmit()) {
                camera->getEventEmit().BlockingCall(
                    eventDataPtr, [](Napi::Env env, Napi::Function jsCallback, ObjectEventData *dataPtr) {
                        Napi::Object event = Napi::Object::New(env);
                        event.Set("camera", CameraWrap::NewInstance(env, dataPtr->camera));
                        switch (dataPtr->eventID) {
                            case kEdsObjectEvent_DirItemRequestTransfer:
                                event.Set(
                                    "file", CameraFileWrap::NewInstance(env, (EdsDirectoryItemRef) dataPtr->objectRef)
                                );
                                jsCallback.Call(
                                    {
                                        Napi::String::New(env, EventName_DownloadRequest),
                                        event
                                    }
                                );
                                break;
                            default:
                                event.Set("objectEvent", ObjectEvent::NewInstance(env, dataPtr->eventID));
                                jsCallback.Call(
                                    {
                                        Napi::String::New(env, EventName_ObjectChange),
                                        event
                                    }
                                );
                                break;
                        }
                        EdsRelease(dataPtr->objectRef);
                        delete dataPtr;
                    }
                );
            }
        } catch (...) {
            EdsRelease(inRef);
        }
        return EDS_ERR_OK;
    }

    /* JS Wrapper Class */

    CameraWrap::CameraWrap(const Napi::CallbackInfo &info)
        : Napi::ObjectWrap<CameraWrap>(info) {

        if (info.Length() > 0) {
            if (info[0].IsExternal()) {
                auto external = info[0].As<Napi::External<CameraReference>>();
                camera_ = *external.Data();
            } else if (info[0].IsNumber()) {
                camera_ = CameraBrowser::instance()->getCameraAtIndex(info[0].As<Napi::Number>().Int32Value());
            } else if (info[0].IsString()) {
                camera_ = CameraBrowser::instance()->getCameraAtPort(info[0].As<Napi::String>().Utf8Value());
            }
        } else {
            camera_ = CameraBrowser::instance()->getCameraAtIndex(0);
        }
        if (!camera_) {
            ApiError::Throw(info.Env(), EDS_ERR_DEVICE_NOT_FOUND);
        }
    }

    CameraWrap::~CameraWrap() {
        camera_ = nullptr;
    }

    Napi::Object CameraWrap::NewInstance(Napi::Env env, CameraReference camera) {
        Napi::EscapableHandleScope scope(env);
        Napi::Object wrap = JSConstructor().New(
            {
                Napi::External<CameraReference>::New(env, &camera)
            }
        );
        return scope.Escape(napi_value(wrap)).ToObject();
    }

    Napi::Object CameraWrap::NewInstance(Napi::Env env, EdsCameraRef edsCameraRef) {
        CameraReference camera = CameraBrowser::instance()->getCameraByEdsReference(edsCameraRef);
        Napi::EscapableHandleScope scope(env);
        Napi::Object wrap = JSConstructor().New(
            {
                Napi::External<CameraReference>::New(env, &camera)
            }
        );
        return scope.Escape(napi_value(wrap)).ToObject();
    }

    Napi::Value CameraWrap::GetDescription(const Napi::CallbackInfo &info) {
        return Napi::String::New(info.Env(), camera_->getDescription());
    }

    Napi::Value CameraWrap::GetPortName(const Napi::CallbackInfo &info) {
        return Napi::String::New(info.Env(), camera_->getPortName());
    }

    Napi::Value CameraWrap::ToStringTag(const Napi::CallbackInfo &info) {
        return Napi::String::New(info.Env(), CameraWrap::JSClassName);
    };

    Napi::Value CameraWrap::Inspect(const Napi::CallbackInfo &info) {
        auto env = info.Env();
        auto stylize = info[1].As<Napi::Object>().Get("stylize").As<Napi::Function>();
        std::string output = stylize.Call(
            {Napi::String::New(env, CameraWrap::JSClassName), Napi::String::New(env, "special")}
        ).As<Napi::String>().Utf8Value();
        output.append(" <");
        output.append(
            stylize.Call(
                {GetDescription(info), Napi::String::New(env, "string")}
            ).As<Napi::String>().Utf8Value()
        );
        output.append(">");
        return Napi::String::New(env, output);
    };

    Napi::Value CameraWrap::Connect(const Napi::CallbackInfo &info) {
        return ApiError::ThrowIfFailed(info.Env(), camera_->connect());
    }

    Napi::Value CameraWrap::Disconnect(const Napi::CallbackInfo &info) {
        return ApiError::ThrowIfFailed(info.Env(), camera_->disconnect());
    }

    Napi::Value CameraWrap::GetProperty(const Napi::CallbackInfo &info) {
        if (info.Length() < 1) {
            throw Napi::TypeError::New(
                info.Env(), "Camera.setProperty(): Argument 0 must be an property identifier."
            );
        }
        return CameraProperty::NewInstance(
            info.Env(),
            camera_,
            info[0].As<Napi::Number>().Int32Value(),
            (info.Length() > 1) ? info[1].As<Napi::Number>().Int32Value() : 0
        );
    }

    Napi::Value CameraWrap::SendCommand(const Napi::CallbackInfo &info) {
        if (info.Length() < 1 || !info[0].IsNumber()) {
            throw Napi::TypeError::New(
                info.Env(), "Camera.sendCommand(): Argument 0 must be an command identifier."
            );
        }
        return ApiError::ThrowIfFailed(
            info.Env(),
            camera_->sendCommand(
                info[0].As<Napi::Number>().Int32Value(),
                (info.Length() > 1) ? info[1].As<Napi::Number>().Int32Value() : 0
            )
        );
    }

    Napi::Value CameraWrap::TakePicture(const Napi::CallbackInfo &info) {
        return ApiError::ThrowIfFailed(info.Env(), camera_->takePicture());
    }

    Napi::Value CameraWrap::StartLiveView(const Napi::CallbackInfo &info) {
        return ApiError::ThrowIfFailed(info.Env(), camera_->startLiveView());
    }

    Napi::Value CameraWrap::StopLiveView(const Napi::CallbackInfo &info) {
        return ApiError::ThrowIfFailed(info.Env(), camera_->stopLiveView());
    }

    Napi::Value CameraWrap::DownloadLiveViewImage(const Napi::CallbackInfo &info) {
        Napi::Env env = info.Env();
        std::string image;
        EdsError error = camera_->downloadLiveViewImage(image);
        return ApiError::ThrowIfFailed(env, error, Napi::String::New(env, image));
    }

    void CameraWrap::Init(Napi::Env env, Napi::Object exports) {
        Napi::HandleScope scope(env);

        Napi::Object Commands = Napi::Object::New(env);
        for (const auto &it : CameraCommands) {
            Commands.Set(
                Napi::String::New(env, it.second), Napi::Number::New(env, it.first)
            );
        }
        Napi::Object ShutterButtonParameters = Napi::Object::New(env);
        for (const auto &it : CameraParametersShutterButton) {
            ShutterButtonParameters.Set(
                Napi::String::New(env, it.second), Napi::Number::New(env, it.first)
            );
        }

        Napi::Function func = DefineClass(
            env,
            CameraWrap::JSClassName,
            {
                InstanceAccessor<&CameraWrap::GetDescription>("description"),
                InstanceAccessor<&CameraWrap::GetPortName>("portName"),

                InstanceAccessor<&CameraWrap::ToStringTag>(Napi::Symbol::WellKnown(env, "toStringTag")),
                InstanceMethod(GetPublicSymbol(env, "nodejs.util.inspect.custom"), &CameraWrap::Inspect),

                InstanceMethod("connect", &CameraWrap::Connect),
                InstanceMethod("disconnect", &CameraWrap::Disconnect),
                InstanceMethod("getProperty", &CameraWrap::GetProperty),
                InstanceMethod("sendCommand", &CameraWrap::SendCommand),
                InstanceMethod("takePicture", &CameraWrap::TakePicture),
                InstanceMethod("startLiveView", &CameraWrap::StartLiveView),
                InstanceMethod("stopLiveView", &CameraWrap::StopLiveView),
                InstanceMethod("downloadLiveViewImage", &CameraWrap::DownloadLiveViewImage),

                StaticValue("Command", Commands, napi_enumerable),
                StaticValue("PressShutterButton", ShutterButtonParameters, napi_enumerable)
            }
        );
        JSConstructor(&func);
        exports.Set(CameraWrap::JSClassName, func);
    }
}
