#pragma once

#include "base/core.h"
#include "base/string.h"
#include "ui/ui.h"

istruct (UiViewInstance);

// The functions get_icon/get_title can be called
// with the UiViewInstance param set to 0, which
// indicates that we want the static title/icon.
istruct (UiViewType) {
    U32 id;
    Void   (*init)      (UiViewInstance *);
    Void   (*free)      (UiViewInstance *);
    UiIcon (*get_icon)  (UiViewInstance *, Bool visible);
    String (*get_title) (UiViewInstance *, Bool visible);
    Void   (*build)     (UiViewInstance *, Bool visible);
};

istruct (UiViewInstance) {
    UiViewType *type;
    Void *data;
};

array_typedef(UiViewInstance*, UiViewInstance);
array_typedef(UiViewType*, UiViewType);

istruct (UiViewStore) {
    Mem *mem;
    ArrayUiViewType types;
    ArrayUiViewInstance instances;
};

UiViewStore    *ui_view_store_new       (Mem *);
Void            ui_view_type_add        (UiViewStore *, UiViewType);
UiViewType     *ui_view_type_get        (UiViewStore *, U32 type_id);
UiViewInstance *ui_view_instance_new    (UiViewStore *, U32 type_id);
Void            ui_view_instance_remove (UiViewStore *, UiViewInstance *);
