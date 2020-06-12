#include "capture.h"
#include "jpegEncode.h"

#include <nan.h>
#include <errno.h>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <x264.h>

namespace {

class MicrosecondBlock {
public:
    MicrosecondBlock(std::string&& id) : m_id(std::move(id)) {
        m_start = std::chrono::high_resolution_clock::now();
        ++m_enter;
    }
    ~MicrosecondBlock() {
        --m_enter;
        auto elapsed = std::chrono::high_resolution_clock::now() - m_start;
        auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
        for(int i = 0; i < m_enter; ++i) {
            std::cout << "  ";
        }
        std::cout << "MSB " << m_id << " completed in " << microseconds << std::endl;
    }

private:
    std::chrono::time_point<std::chrono::high_resolution_clock> m_start;
    std::string m_id;
    static int m_enter;
};
int MicrosecondBlock::m_enter = 0;

struct CallbackData {
  Nan::Persistent<v8::Object> thisObj;
  std::unique_ptr<Nan::Callback> callback;
};

class Camera : public Nan::ObjectWrap {
public:
  static NAN_MODULE_INIT(Init);
private:
  static NAN_METHOD(New);
  static NAN_METHOD(Start);
  static NAN_METHOD(Stop);
  static NAN_METHOD(Capture);
  static NAN_METHOD(FrameRaw);
  static NAN_METHOD(FrameYUYVToRGB);
  static NAN_METHOD(FrameYUYVToJPEG);
  static NAN_METHOD(AsyncYUYVToJPEG);
  static NAN_METHOD(FrameYUYVToYUV);
  static NAN_METHOD(ConfigGet);
  static NAN_METHOD(ConfigSet);
  static NAN_METHOD(ControlGet);
  static NAN_METHOD(ControlSet);
  
  static void StopCB(uv_poll_t* handle, int status, int events);
  static void CaptureCB(uv_poll_t* handle, int status, int events);
  
  static void
  WatchCB(uv_poll_t* handle, void (*callbackCall)(CallbackData* data));
  static void
  Watch(const Nan::FunctionCallbackInfo<v8::Value>& info, uv_poll_cb cb);
  
  Camera();
  ~Camera();

  static inline Nan::Persistent<v8::Function>& constructor() {
    static Nan::Persistent<v8::Function> my_constructor;
    return my_constructor;
  }

  camera_t* camera;
};

//[error message handling]
struct LogContext {
  std::string msg;
};
static void logRecord(camera_log_t type, const char* msg, void* pointer) {
  std::stringstream ss;
  switch (type) {
  case CAMERA_ERROR:
    ss << "CAMERA ERROR [" << msg << "] " << errno << " " << strerror(errno);
    break;
  case CAMERA_FAIL:
    ss << "CAMERA FAIL [" << msg << "]";
    break;
  case CAMERA_INFO:
    ss << "CAMERA INFO [" << msg << "]";
    break;
  }
  static_cast<LogContext*>(pointer)->msg = ss.str();
}

static inline v8::Local<v8::Value> cameraError(const camera_t* camera) {
  const auto ctx = static_cast<LogContext*>(camera->context.pointer);
  return Nan::Error(ctx->msg.c_str());
}


//[helpers]
static inline v8::Local<v8::Value>
getValue(const v8::Local<v8::Object>& self, const char* name) {
  return Nan::Get(self, Nan::New(name).ToLocalChecked()).ToLocalChecked();
}
static inline std::int32_t
getInt(const v8::Local<v8::Object>& self, const char* name) {
  return Nan::To<std::int32_t>(getValue(self, name)).FromJust();
}
static inline std::uint32_t
getUint(const v8::Local<v8::Object>& self, const char* name) {
  return Nan::To<std::uint32_t>(getValue(self, name)).FromJust();
}

static inline void 
setValue(const v8::Local<v8::Object>& self, const char* name, 
          const v8::Local<v8::Value>& value) {
  Nan::Set(self, Nan::New(name).ToLocalChecked(), value);
}
static inline void 
setInt(const v8::Local<v8::Object>& self, const char* name,
        std::int32_t value) {
  setValue(self, name, Nan::New(value));
}
static inline void 
setUint(const v8::Local<v8::Object>& self, const char* name, 
        std::uint32_t value) {
  setValue(self, name, Nan::New(value));
}
static inline void 
setString(const v8::Local<v8::Object>& self, const char* name, 
          const char* value) {
  setValue(self, name, Nan::New(value).ToLocalChecked());
}
static inline void 
setBool(const v8::Local<v8::Object>& self, const char* name, bool value) {
  setValue(self, name, Nan::New<v8::Boolean>(value));
}

//[callback helpers]
void Camera::WatchCB(uv_poll_t* handle,
                      void (*callbackCall)(CallbackData* data)) {
  Nan::HandleScope scope;
  auto data = static_cast<CallbackData*>(handle->data);
  uv_poll_stop(handle);
  uv_close(reinterpret_cast<uv_handle_t*>(handle), 
            [](uv_handle_t* handle) -> void {delete handle;});
  
  callbackCall(data);
  data->thisObj.Reset();
  delete data;
}
void Camera::Watch(const Nan::FunctionCallbackInfo<v8::Value>& info,
                    uv_poll_cb cb) {
  auto data = new CallbackData;
  data->thisObj.Reset(info.Holder());
  data->callback.reset(new Nan::Callback(info[0].As<v8::Function>()));
  auto camera = Nan::ObjectWrap::Unwrap<Camera>(info.Holder())->camera;
  
  auto handle = new uv_poll_t;
  handle->data = data;
  uv_poll_init(uv_default_loop(), handle, camera->fd);
  uv_poll_start(handle, UV_READABLE, cb);
}

class JPEGCompressWorker : public Nan::AsyncWorker {
public:
  JPEGCompressWorker(camera_t* camera, Nan::Callback* callback)
    : m_camera(camera), Nan::AsyncWorker(callback) {
      m_isBusy = true;
  }

  static bool IsBusy() {
    return m_isBusy;
  }

  void Execute() override {
    if (!m_camera->initialized) {
      this->SetErrorMessage("Error: camera is not initialized.");
      return;
    }

    //auto jpegInfo = tjYUYVtoJPEG(m_camera->head.start, m_camera->width, m_camera->height);
    auto jpegInfo = compressYUYVtoJPEG(m_camera->head.start, m_camera->width, m_camera->height);
    m_bufferSize = jpegInfo.first;
    m_buffer = jpegInfo.second;
  }

  void HandleOKCallback() override {
    Nan::HandleScope scope;
    // const auto flag = v8::ArrayBufferCreationMode::kInternalized;
    // auto buf = v8::ArrayBuffer::New(info.GetIsolate(), m_buffer, m_bufferSize, flag);
    // auto array = v8::Uint8Array::New(buf, 0, m_bufferSize);
    auto array = Nan::NewBuffer(reinterpret_cast<char*>(m_buffer), m_bufferSize);
    v8::Local<v8::Value> argv[] = {
      Nan::Null(), // no error occured
      array.ToLocalChecked()
    };
    Nan::Call(callback->GetFunction(), Nan::GetCurrentContext()->Global(), 2, argv);
    m_isBusy = false;
  }

  void HandleErrorCallback() override {
    Nan::HandleScope scope;
    v8::Local<v8::Value> argv[] = {
      Nan::New(this->ErrorMessage()).ToLocalChecked(), // return error message
      Nan::Null()
    };
    Nan::Call(callback->GetFunction(), Nan::GetCurrentContext()->Global(), 2, argv);
    m_isBusy = false;
  }
private:
  static bool m_isBusy;
  camera_t* m_camera { nullptr };
  unsigned char* m_buffer { nullptr };
  unsigned long m_bufferSize { 0 };
};
bool JPEGCompressWorker::m_isBusy = false;

//[methods]

static const char* control_type_names[] = {
  "invalid",
  "int",
  "bool",
  "menu",
  "int64",
  "class"
  "string",
  "bitmask",
  "int_menu",
};

static v8::Local<v8::Object> cameraControls(const camera_t* camera) {
  auto ccontrols = camera_controls_new(camera);
  auto controls = Nan::New<v8::Array>(ccontrols->length);
  for (auto i = std::size_t{0}; i < ccontrols->length; ++i) {
    const auto ccontrol = &ccontrols->head[i];
    auto control = Nan::New<v8::Object>();
    const auto name =
      Nan::New(reinterpret_cast<char*>(ccontrol->name)).ToLocalChecked();
    Nan::Set(controls, i, control);
    Nan::Set(controls, name, control);
    setUint(control, "id", ccontrol->id);
    setValue(control, "name", name);
    setString(control, "type", control_type_names[ccontrol->type]);
    setInt(control, "min", ccontrol->min);
    setInt(control, "max", ccontrol->max);
    setInt(control, "step", ccontrol->step);
    setInt(control, "default", ccontrol->default_value);
    
    auto flags = Nan::New<v8::Object>();
    setValue(control, "flags", flags);
    setBool(flags, "disabled", ccontrol->flags.disabled);
    setBool(flags, "grabbed", ccontrol->flags.grabbed);
    setBool(flags, "readOnly", ccontrol->flags.read_only);
    setBool(flags, "update", ccontrol->flags.update);
    setBool(flags, "inactive", ccontrol->flags.inactive);
    setBool(flags, "slider", ccontrol->flags.slider);
    setBool(flags, "writeOnly", ccontrol->flags.write_only);
    setBool(flags, "volatile", ccontrol->flags.volatile_value);
    
    auto menu = Nan::New<v8::Array>(ccontrol->menus.length);
    setValue(control, "menu", menu);
    switch (ccontrol->type) {
    case CAMERA_CTRL_MENU:
      for (auto j = std::size_t{0}; j < ccontrol->menus.length; ++j) {
        const auto value =
          reinterpret_cast<char*>(ccontrol->menus.head[j].name);
        Nan::Set(menu, j, Nan::New(value).ToLocalChecked());
      }
      break;
#ifndef CAMERA_OLD_VIDEODEV2_H
    case CAMERA_CTRL_INTEGER_MENU:
      for (auto j = std::size_t{0}; j < ccontrol->menus.length; ++j) {
        const auto value =
          static_cast<std::int32_t>(ccontrol->menus.head[j].value);
        Nan::Set(menu, j, Nan::New(value));
      }
      break;
#endif
    default: break;
    }
  }
  camera_controls_delete(ccontrols);
  return controls;
}

static camera_format_t convertCFormat(const v8::Local<v8::Object> format) {
  const auto pixformat = getUint(format, "format");
  const auto width = getUint(format, "width");
  const auto height = getUint(format, "height");
  auto numerator = std::uint32_t{0};
  auto denominator = std::uint32_t{0};
  const auto finterval = getValue(format, "interval");
  if (finterval->IsObject()) {
    const auto interval = finterval->ToObject();
    numerator = getUint(interval, "numerator");
    denominator = getUint(interval, "denominator");
  }
  return {
    pixformat, width, height, {numerator, denominator}
  };
}

static v8::Local<v8::Object> convertFormat(const camera_format_t* cformat) {
  char name[5];
  camera_format_name(cformat->format, name);
  auto format = Nan::New<v8::Object>();
  setString(format, "formatName", name);
  setUint(format, "format", cformat->format);
  setUint(format, "width", cformat->width);
  setUint(format, "height", cformat->height);
  auto interval = Nan::New<v8::Object>();
  setValue(format, "interval", interval);
  setUint(interval, "numerator", cformat->interval.numerator);
  setUint(interval, "denominator", cformat->interval.denominator);
  return format;
}

static v8::Local<v8::Object> cameraFormats(const camera_t* camera) {
  auto cformats = camera_formats_new(camera);
  auto formats = Nan::New<v8::Array>(cformats->length);
  for (auto i = std::size_t{0}; i < cformats->length; ++i) {
    auto cformat = &cformats->head[i];
    auto format = convertFormat(cformat);
    Nan::Set(formats, i, format);
  }
  return formats;
}

NAN_METHOD(Camera::New) {
  if (!info.IsConstructCall()) {
    // [NOTE] generic recursive call with `new`
    v8::Local<v8::Function> cons = Nan::New(constructor());
    std::vector<v8::Local<v8::Value>> args(info.Length());
    for (auto i = std::size_t{0}; i < args.size(); ++i) args[i] = info[i];
    auto inst = Nan::NewInstance(cons, args.size(), args.data());
    if (!inst.IsEmpty()) info.GetReturnValue().Set(inst.ToLocalChecked());
    return;
  }

  if (info.Length() < 1) {
    Nan::ThrowTypeError("argument required: device");
    return;
  }
  const auto device = Nan::To<v8::String>(info[0]).ToLocalChecked();
  auto camera = camera_open(*Nan::Utf8String(device));
  if (!camera) {
    Nan::ThrowError(strerror(errno));
    return;
  }
  camera->context.pointer = new LogContext;
  camera->context.log = &logRecord;
  
  auto thisObj = info.This();
  auto self = new Camera;
  self->camera = camera;
  self->Wrap(thisObj);
  setValue(thisObj, "device", info[0]);
  setValue(thisObj, "formats", cameraFormats(camera));
  setValue(thisObj, "controls", cameraControls(camera));
}

NAN_METHOD(Camera::Start) {
  auto thisObj = info.Holder();
  auto camera = Nan::ObjectWrap::Unwrap<Camera>(thisObj)->camera;
  if (!camera_start(camera)) {
    Nan::ThrowError(cameraError(camera));
    return;
  }
  setUint(thisObj, "width", camera->width);
  setUint(thisObj, "height", camera->height);
  info.GetReturnValue().Set(thisObj);
}


void Camera::StopCB(uv_poll_t* handle, int /*status*/, int /*events*/) {
  auto callCallback = [](CallbackData* data) -> void {
    Nan::HandleScope scope;
    auto thisObj = Nan::New<v8::Object>(data->thisObj);
    data->callback->Call(thisObj, 0, nullptr);
  };
  WatchCB(handle, callCallback);
}
NAN_METHOD(Camera::Stop) {
  auto camera = Nan::ObjectWrap::Unwrap<Camera>(info.Holder())->camera;
  if (!camera_stop(camera)) {
    Nan::ThrowError(cameraError(camera));
    return;
  }
  if(info.Length() > 0) {
    Watch(info, StopCB);
  }
}

void Camera::CaptureCB(uv_poll_t* handle, int /*status*/, int /*events*/) {
  auto callCallback = [](CallbackData* data) -> void {
    Nan::HandleScope scope;
    auto thisObj = Nan::New<v8::Object>(data->thisObj);
    auto camera = Nan::ObjectWrap::Unwrap<Camera>(thisObj)->camera;
    auto captured = bool{camera_capture(camera)};
    std::vector<v8::Local<v8::Value>> args{{Nan::New(captured)}};
    data->callback->Call(thisObj, args.size(), args.data());
  };
  WatchCB(handle, callCallback);
}
NAN_METHOD(Camera::Capture) {
  Watch(info, CaptureCB);
}


NAN_METHOD(Camera::FrameRaw) {
  const auto camera = Nan::ObjectWrap::Unwrap<Camera>(info.Holder())->camera;
  const auto size = camera->head.length;
  auto data = new uint8_t[size];
  std::copy(camera->head.start, camera->head.start + size, data);
  const auto flag = v8::ArrayBufferCreationMode::kInternalized;
  auto buf = v8::ArrayBuffer::New(info.GetIsolate(), data, size, flag);
  auto array = v8::Uint8Array::New(buf, 0, size);
  info.GetReturnValue().Set(array);
}

NAN_METHOD(Camera::FrameYUYVToRGB) {
  // TBD: check the current format as YUYV
  const auto camera = Nan::ObjectWrap::Unwrap<Camera>(info.Holder())->camera;
  auto rgb = yuyv2rgb(camera->head.start, camera->width, camera->height);
  const auto size = camera->width * camera->height * 3;
  const auto flag = v8::ArrayBufferCreationMode::kInternalized;
  auto buf = v8::ArrayBuffer::New(info.GetIsolate(), rgb, size, flag);
  auto array = v8::Uint8Array::New(buf, 0, size);
  info.GetReturnValue().Set(array);
}

NAN_METHOD(Camera::FrameYUYVToJPEG) {
  const auto camera = Nan::ObjectWrap::Unwrap<Camera>(info.Holder())->camera;
  //auto jpegInfo = tjYUYVtoJPEG(camera->head.start, camera->width, camera->height);
  auto jpegInfo = compressYUYVtoJPEG(camera->head.start, camera->width, camera->height);
  const auto size = jpegInfo.first;
  const auto jpeg = jpegInfo.second;
  const auto flag = v8::ArrayBufferCreationMode::kInternalized;
  auto buf = v8::ArrayBuffer::New(info.GetIsolate(), jpeg, size, flag);
  auto array = v8::Uint8Array::New(buf, 0, size);
  info.GetReturnValue().Set(array);
}

NAN_METHOD(Camera::AsyncYUYVToJPEG) {
  if(JPEGCompressWorker::IsBusy()) {
    return;
  }
  if(!info[0]->IsFunction()) {
    return Nan::ThrowError(Nan::New("expected arg 0: function callback").ToLocalChecked());
  }
  const auto camera = Nan::ObjectWrap::Unwrap<Camera>(info.Holder())->camera;
  Nan::AsyncQueueWorker(new JPEGCompressWorker(
    camera,
    new Nan::Callback(info[0].As<v8::Function>())
  ));
}

NAN_METHOD(Camera::FrameYUYVToYUV) {
  const auto camera = Nan::ObjectWrap::Unwrap<Camera>(info.Holder())->camera;
  auto yuvInfo = tjYUYVtoYUV(camera->head.start, camera->width, camera->height);
  const auto size = yuvInfo.first;
  const auto yuv = yuvInfo.second;
  const auto flag = v8::ArrayBufferCreationMode::kInternalized;
  auto buf = v8::ArrayBuffer::New(info.GetIsolate(), yuv, size, flag);
  auto array = v8::Uint8Array::New(buf, 0, size);
  info.GetReturnValue().Set(array);
}


NAN_METHOD(Camera::ConfigGet) {
  const auto camera = Nan::ObjectWrap::Unwrap<Camera>(info.Holder())->camera;
  camera_format_t cformat;
  if (!camera_config_get(camera, &cformat)) {
    Nan::ThrowError(cameraError(camera));
    return;
  }
  auto format = convertFormat(&cformat);
  info.GetReturnValue().Set(format);
}

NAN_METHOD(Camera::ConfigSet) {
  if (info.Length() < 1) {
    Nan::ThrowTypeError("argument required: config");
    return;
  }
  const auto cformat = convertCFormat(info[0]->ToObject());
  auto thisObj = info.Holder();
  auto camera = Nan::ObjectWrap::Unwrap<Camera>(thisObj)->camera;
  if (!camera_config_set(camera, &cformat)) {
    Nan::ThrowError(cameraError(camera));
    return;
  }
  setUint(thisObj, "width", camera->width);
  setUint(thisObj, "height", camera->height);
  info.GetReturnValue().Set(thisObj);
}

NAN_METHOD(Camera::ControlGet) {
  if (info.Length() < 1) {
    Nan::ThrowTypeError("an argument required: id");
    return;
  }
  const auto id = info[0]->Uint32Value();
  auto camera = Nan::ObjectWrap::Unwrap<Camera>(info.Holder())->camera;
  auto value = std::int32_t{0};
  auto success = bool{camera_control_get(camera, id, &value)};
  if (!success) {
    Nan::ThrowError(cameraError(camera));
    return;
  }
  info.GetReturnValue().Set(Nan::New(value));
}

NAN_METHOD(Camera::ControlSet) {
  if (info.Length() < 2) {
    Nan::ThrowTypeError("arguments required: id, value");
    return;
  }
  const auto id = info[0]->Uint32Value();
  const auto value = info[1]->Int32Value();
  auto thisObj = info.Holder();
  auto camera = Nan::ObjectWrap::Unwrap<Camera>(thisObj)->camera;
  auto success = bool{camera_control_set(camera, id, value)};
  if (!success) {
    Nan::ThrowError(cameraError(camera));
    return;
  }
  info.GetReturnValue().Set(thisObj);
}


Camera::Camera() : camera(nullptr) {}
Camera::~Camera() {
  if (camera) {
    auto ctx = static_cast<LogContext*>(camera->context.pointer);
    camera_close(camera);
    delete ctx;
  }
}

//[module init]
NAN_MODULE_INIT(Camera::Init) {
  const auto name = Nan::New("Camera").ToLocalChecked();
  auto ctor = Nan::New<v8::FunctionTemplate>(New);
  auto ctorInst = ctor->InstanceTemplate();
  ctor->SetClassName(name);
  ctorInst->SetInternalFieldCount(1);
  
  Nan::SetPrototypeMethod(ctor, "start", Start);
  Nan::SetPrototypeMethod(ctor, "stop", Stop);
  Nan::SetPrototypeMethod(ctor, "capture", Capture);
  Nan::SetPrototypeMethod(ctor, "frameRaw", FrameRaw);
  Nan::SetPrototypeMethod(ctor, "toRGB", FrameYUYVToRGB);
  Nan::SetPrototypeMethod(ctor, "toJPEG", FrameYUYVToJPEG);
  Nan::SetPrototypeMethod(ctor, "toJPEGAsync", AsyncYUYVToJPEG);
  Nan::SetPrototypeMethod(ctor, "toYUV", FrameYUYVToYUV);
  Nan::SetPrototypeMethod(ctor, "configGet", ConfigGet);
  Nan::SetPrototypeMethod(ctor, "configSet", ConfigSet);
  Nan::SetPrototypeMethod(ctor, "controlGet", ControlGet);
  Nan::SetPrototypeMethod(ctor, "controlSet", ControlSet);
  Nan::Set(target, name, Nan::GetFunction(ctor).ToLocalChecked());
}
}

NODE_MODULE(v4l2camera, Camera::Init)
