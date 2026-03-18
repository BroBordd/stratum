#define __BIONIC_AVAILABILITY_GUARD(x) 1

// block problematic v31 input headers
#define ANDROID_GUI_INPUTWINDOW_H
#define ANDROID_INPUT_INPUTWINDOW_H
#define ANDROID_INPUT_INPUTAPPLICATION_H
#define ANDROID_FOCUSREQUEST_H
#define ANDROID_INPUTAPPLICATIONINFO_H
#define _LIBINPUT_INPUT_H
#define _LIBINPUT_INPUT_TRANSPORT_H
#define _UI_INPUT_WINDOW_H

// forward declare blocked types
#include <utils/StrongPointer.h>
namespace android {
    class InputWindowHandle;
    struct InputWindowInfo;
    struct FocusRequest;
}

#pragma clang diagnostic ignored "-Winvalid-specialization"
#define NO_INPUT
