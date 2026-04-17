#pragma once

#include "base/core.h"
#include "ui/ui.h"
#include "ui/ui_view.h"

Void   settings_view_init      (UiViewInstance *);
Void   settings_view_free      (UiViewInstance *);
UiIcon settings_view_get_icon  (UiViewInstance *, Bool visible);
String settings_view_get_title (UiViewInstance *, Bool visible);
Void   settings_view_build     (UiViewInstance *, Bool visible);
