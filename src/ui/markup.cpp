#include "ui/main.h"
#include "ui/util.h"
#include "ui/widgets.h"
#include "core/markup.h"
#include "ui/markup.h"
#include "os/time.h"

struct ClickableInline {
    MarkupAst *node;
    U64 start;
    U64 end;
};

struct ParagraphContext {
    UiMarkup *m;
    U64 length;
    GtkWidget *label;
    ClickableInline hovered_clickable;
    Array<ClickableInline> clickables;
};

struct UiMarkupPosition {
    U64 idx;
    String text;
    Slice<MarkupAst*> ast_path;
    Array<GtkWidget*> widget_path;
};

static GtkWidget *render_block (UiMarkup *m, MarkupAst *node);

static Void render_inline (UiMarkup *m, AString *markup, MarkupAst *node) {
    switch (node->tag) {
    case MARKUP_AST_TEXT: {
        String text = markup_ast_get_text(node, m->text);
        String escaped_text = str(g_markup_escape_text(text.data, text.count));
        astr_push_str(markup, escaped_text);
        m->paragraph_context->length += escaped_text.count;
        g_free(escaped_text.data);
    } break;
    case MARKUP_AST_ITALIC: {
        astr_push_cstr(markup, "<i>");
        array_iter (child, &node->children) render_inline(m, markup, child);
        astr_push_cstr(markup, "</i>");
    } break;
    case MARKUP_AST_BOLD: {
        astr_push_cstr(markup, "<b>");
        array_iter (child, &node->children) render_inline(m, markup, child);
        astr_push_cstr(markup, "</b>");
    } break;
    case MARKUP_AST_HIGHLIGHT: {
        astr_push_fmt(markup, "<b><span bgcolor=\"%.*s\"> ", STR(main_context.error_color));
        m->paragraph_context->length += 1;
        array_iter (child, &node->children) render_inline(m, markup, child);
        m->paragraph_context->length += 1;
        astr_push_cstr(markup, " </span></b>");
    } break;
    case MARKUP_AST_SUP: {
        astr_push_cstr(markup, "<sup>");
        array_iter (child, &node->children) render_inline(m, markup, child);
        astr_push_cstr(markup, "</sup>");
    } break;
    case MARKUP_AST_SUB: {
        astr_push_cstr(markup, "<sub>");
        array_iter (child, &node->children) render_inline(m, markup, child);
        astr_push_cstr(markup, "</sub>");
    } break;
    case MARKUP_AST_STRIKE: {
        astr_push_cstr(markup, "<s>");
        array_iter (child, &node->children) render_inline(m, markup, child);
        astr_push_cstr(markup, "</s>");
    } break;
    case MARKUP_AST_RAW_INLINE: {
        Bool monospace = reinterpret_cast<MarkupAstRawInline*>(node)->monospace;

        if (monospace) {
            astr_push_fmt(markup, "<span bgcolor=\"%.*s\"> ", STR(main_context.base_color));
            m->paragraph_context->length += 1;
        }

        array_iter (child, &node->children) render_inline(m, markup, child);

        if (monospace) {
            m->paragraph_context->length += 1;
            astr_push_cstr(markup, " </span>");
        }
    } break;
    case MARKUP_AST_LINK: {
        Auto n = reinterpret_cast<MarkupAstLink*>(node);
        astr_push_fmt(markup, "<u><span color=\"%.*s\">", STR(main_context.accent_color));

        U64 start = m->paragraph_context->length;

        if (n->alias.data) {
            String escaped_text = str(g_markup_escape_text(n->alias.data, n->alias.count));
            astr_push_str(markup, escaped_text);
            m->paragraph_context->length += escaped_text.count;
            g_free(escaped_text.data);
        } else if (n->link.data) {
            String escaped_text = str(g_markup_escape_text(n->link.data, n->link.count));
            astr_push_str(markup, escaped_text);
            m->paragraph_context->length += escaped_text.count;
            g_free(escaped_text.data);
        }

        array_push_lit(&m->paragraph_context->clickables, .node=node, .start=start, .end=m->paragraph_context->length);
        astr_push_cstr(markup, "</span></u>");
    } break;
    case MARKUP_AST_TAG_REF: {
        astr_push_fmt(markup, "<b><span color=\"%.*s\">", STR(main_context.warning_color));
        U64 start = m->paragraph_context->length;
        array_iter (child, &node->children) render_inline(m, markup, child);
        array_push_lit(&m->paragraph_context->clickables, .node=node, .start=start, .end=m->paragraph_context->length);
        astr_push_cstr(markup, "</span></b>");
    } break;
    default: badpath;
    }
}

static GtkWidget *render_paragraph (UiMarkup *m, MarkupAst *node) {
    tmem_new(tm);

    Auto p = mem_new(m->mem, ParagraphContext);
    p->m = m;
    array_init(&p->clickables, m->mem);
    m->paragraph_context = p;

    AString markup = astr_new(tm);
    array_iter (child, &node->children) render_inline(m, &markup, child);
    astr_push_byte(&markup, 0);

    String text = markup_ast_get_text(node, m->text);
    if (array_get_last(&text) == '\n') {
        U64 idx = str_index_of_last(astr_to_str(&markup), '\n');
        array_remove(&markup, idx);
    }

    GtkWidget *w = gtk_label_new(0);
    gtk_label_set_max_width_chars(GTK_LABEL(w), 100);
    gtk_label_set_markup(GTK_LABEL(w), markup.data);
    gtk_widget_add_css_class(w, "paragraph");
    gtk_widget_add_css_class(w, "block");
    gtk_widget_set_halign(w, GTK_ALIGN_START);
    p->label = w;

    //
    // listen
    //
    GtkGesture *click = gtk_gesture_click_new();
    GtkEventController *motion = gtk_event_controller_motion_new();
    gtk_widget_add_controller(w, GTK_EVENT_CONTROLLER(click));
    gtk_widget_add_controller(w, motion);
    g_signal_connect(motion, "motion", G_CALLBACK(+[](GtkEventController *controller, F64 x, F64 y, gpointer data){
        Auto p = static_cast<ParagraphContext*>(data);
        PangoLayout *layout = gtk_label_get_layout(GTK_LABEL(p->label));

        Int lx;
        Int ly;
        gtk_label_get_layout_offsets(GTK_LABEL(p->label), &lx, &ly);

        Int idx;
        Int trailing;
        Bool inside = pango_layout_xy_to_index(layout, (x - lx) * PANGO_SCALE, (y - ly) * PANGO_SCALE, &idx, &trailing);

        p->hovered_clickable = {};
        if (inside) {
            array_iter (c, &p->clickables) {
                if (idx >= static_cast<Int>(c.start) && idx < static_cast<Int>(c.end)) {
                    p->hovered_clickable = c;
                    ui_set_cursor("pointer");
                    return;
                }
            }

            ui_set_cursor("default");
        }
    }), p);
    g_signal_connect(click, "pressed", G_CALLBACK(+[](GtkGestureClick *gesture, int n_press, F64 x, F64 y, gpointer data){
        Auto p = static_cast<ParagraphContext*>(data);
        if (p->hovered_clickable.node) {
            if (p->hovered_clickable.node->tag == MARKUP_AST_LINK) {
                Auto l = reinterpret_cast<MarkupAstLink*>(p->hovered_clickable.node)->link;
                if      (str_starts_with(l, str("/"))) ui_open_file_uri(l);
                else if (str_starts_with(l, str("~"))) ui_open_file_uri(l);
                else                                   ui_open_web_uri(l);
            } else if (p->hovered_clickable.node->tag == MARKUP_AST_TAG_REF) {
                if (p->m->on_tag_clicked) {
                    String text = markup_ast_get_text(p->hovered_clickable.node, p->m->text);
                    p->m->on_tag_clicked(text);
                }
            }
        }
    }), p);
    g_signal_connect(motion, "leave", G_CALLBACK(+[](GtkEventController *controller, gpointer data){
        ui_set_cursor("default");
    }), p);

    return w;
}

static GtkWidget *render_meta (UiMarkup *m, MarkupAst *node) {
    Auto n = reinterpret_cast<MarkupAstMeta*>(node);

    if (n->config->flags & MARKUP_AST_META_CONFIG_HAS_IMAGE_PATH) {
        U64 width = (n->config->flags & MARKUP_AST_META_CONFIG_HAS_IMAGE_WIDTH) ? n->config->image_width : 0;
        UiImage *image = ui_image_new(m->mem, n->config->image_path, width);
        gtk_widget_add_css_class(image->widget, "block");
        return image->widget;
    } else if (n->config->flags & MARKUP_AST_META_CONFIG_HAS_ADMONITION) {
        GtkWidget *w = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_add_css_class(w, "block");
        gtk_widget_add_css_class(w, "admonition");
        gtk_widget_add_css_class(w, "frame");

        switch (n->config->admonition) {
        case MARKUP_AST_ADMONITION_TIP:       gtk_widget_add_css_class(w, "tip"); break;
        case MARKUP_AST_ADMONITION_NOTE:      gtk_widget_add_css_class(w, "note"); break;
        case MARKUP_AST_ADMONITION_WARNING:   gtk_widget_add_css_class(w, "warning"); break;
        case MARKUP_AST_ADMONITION_IMPORTANT: gtk_widget_add_css_class(w, "important"); break;
        }

        GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, SPACING);
        gtk_box_append(GTK_BOX(w), header);
        gtk_widget_add_css_class(header, "header");
        gtk_widget_add_css_class(header, "frame");

        GtkWidget *icon = gtk_image_new_from_icon_name("kronomi-issue-symbolic");
        gtk_box_append(GTK_BOX(header), icon);

        switch (n->config->admonition) {
        case MARKUP_AST_ADMONITION_TIP:       gtk_box_append(GTK_BOX(header), gtk_label_new(tr("Tip"))); break;
        case MARKUP_AST_ADMONITION_NOTE:      gtk_box_append(GTK_BOX(header), gtk_label_new(tr("Note"))); break;
        case MARKUP_AST_ADMONITION_WARNING:   gtk_box_append(GTK_BOX(header), gtk_label_new(tr("Warning"))); break;
        case MARKUP_AST_ADMONITION_IMPORTANT: gtk_box_append(GTK_BOX(header), gtk_label_new(tr("Important"))); break;
        }

        GtkWidget *body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_box_append(GTK_BOX(w), body);
        gtk_widget_add_css_class(body, "bod");
        array_iter (child, &node->children) gtk_box_append(GTK_BOX(body), render_block(m, child));

        return w;
    } else if (n->config->flags & MARKUP_AST_META_CONFIG_HAS_SPOILER) {
        tmem_new(tm);
        UiExpander *e = ui_expander_new(m->mem, cstr(tm, n->config->spoiler));
        array_iter (child, &node->children) gtk_box_append(GTK_BOX(e->content), render_block(m, child));
        gtk_widget_add_css_class(e->widget, "block");
        return e->widget;
    } else {
        GtkWidget *w = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_add_css_class(w, "block");
        array_iter (child, &node->children) gtk_box_append(GTK_BOX(w), render_block(m, child));
        return w;
    }
}

static GtkWidget *render_table (UiMarkup *m, MarkupAst *node) {
    tmem_new(tm);

    GtkWidget *grid = gtk_grid_new();
    gtk_widget_add_css_class(grid, "block");
    gtk_widget_add_css_class(grid, "table");

    U64 col_count = array_get(&node->children, 0)->children.count;
    U64 row_count = node->children.count;

    // This map tells whether the nth cell of the current row is
    // occupied by another cell due to a cell spanning multiple
    // rows or columns. If the value here is greater than 0, the
    // cell is occupied, and the number indicates how many more
    // cells below this one (in the same column) are occupied.
    Auto occupied = array_new_count<U32>(tm, col_count);

    // This map tells whether the cells to the left and above
    // the current one are visible or not.
    Auto visible = array_new_count<Bool>(tm, col_count);

    Auto dummy_cell = markup_ast_alloc(m->mem, MARKUP_AST_TABLE_CELL);
    reinterpret_cast<MarkupAstTableCell*>(dummy_cell)->config = default_table_cell_config;

    array_iter (row, &node->children) {
        U64 row_idx = ARRAY_IDX;
        U64 remaining_rows = row_count - row_idx;

        // Add missing cells in this row in the form of dummy cells.
        U64 prev_row_length = row->children.count;
        while (row->children.count < col_count) array_push(&row->children, dummy_cell);

        // Render the cells of this row.
        array_iter (cell, &row->children) {
            U64 col_idx = ARRAY_IDX;
            MarkupAstTableCellConfig *config = &reinterpret_cast<MarkupAstTableCell*>(cell)->config;

            if (col_idx == col_count) break; // Ignore excess cells.
            if (array_get(&occupied, col_idx)) continue;

            Bool above_cell_is_visible = array_get(&visible, col_idx);
            Bool left_cell_is_visible  = col_idx && array_get(&visible, col_idx-1);

            U64 final_height = (config->height == -1) ? remaining_rows : min(static_cast<U64>(config->height), remaining_rows);
            U64 final_width  = 0;

            { // Compute final width and update occupied and visible maps:
                U64 remaining_cols  = col_count - col_idx;
                U64 preferred_width = (config->width == -1) ? remaining_cols : min(static_cast<U64>(config->width), remaining_cols);

                for (U64 w=0, idx=col_idx; idx < col_count; w++, idx++) {
                    if (array_get(&occupied, idx) || (w == preferred_width)) break;
                    array_set(&occupied, idx, final_height);
                    array_set(&visible, idx, !config->no_bg);
                    final_width++;
                }
            }

            { // Add cell widget:
                GtkWidget *cell_widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
                gtk_widget_add_css_class(cell_widget, "frame");
                gtk_widget_add_css_class(cell_widget, "cell");
                gtk_widget_set_hexpand(cell_widget, true);
                gtk_widget_set_halign(cell_widget, GTK_ALIGN_FILL);
                map_add(&m->ast_to_widget, cell, cell_widget);

                if (config->no_bg) {
                    gtk_widget_add_css_class(cell_widget, "invisible");
                    if (! above_cell_is_visible) gtk_widget_add_css_class(cell_widget, "no-top-border");
                    if (! left_cell_is_visible)  gtk_widget_add_css_class(cell_widget, "no-left-border");
                }

                if (row_idx == 0) gtk_widget_add_css_class(cell_widget, "first-row");
                if (row_idx + final_height == row_count) gtk_widget_add_css_class(cell_widget, "last-row");
                if (col_idx == 0) gtk_widget_add_css_class(cell_widget, "first-col");
                if (col_idx + final_width == col_count) gtk_widget_add_css_class(cell_widget, "last-col");

                array_iter (child, &cell->children) gtk_box_append(GTK_BOX(cell_widget), render_block(m, child));
                gtk_grid_attach(GTK_GRID(grid), cell_widget, col_idx, row_idx, final_width, final_height);
            }
        }

        row->children.count = prev_row_length;
        array_iter_ptr (x, &occupied) if (*x) *x -= 1;
    }

    return grid;
}

static GtkWidget *render_block (UiMarkup *m, MarkupAst *node) {
    GtkWidget *result = 0;

    switch (node->tag) {
    case MARKUP_AST_META:      result = render_meta(m, node); break;
    case MARKUP_AST_TABLE:     result = render_table(m, node); break;
    case MARKUP_AST_PARAGRAPH: result = render_paragraph(m, node); break;

    case MARKUP_AST_LIST:
    case MARKUP_AST_ORDERED_LIST: {
        Bool ordered = (node->tag == MARKUP_AST_ORDERED_LIST);

        GtkWidget *w = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_add_css_class(w, "block");
        gtk_widget_add_css_class(w, ordered ? "ordered-list" : "list");

        array_iter (child, &node->children) {
            tmem_new(tm);

            GtkWidget *item = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
            gtk_box_append(GTK_BOX(w), item);
            map_add(&m->ast_to_widget, child, item);

            CString bullet_str = ordered ? astr_fmt(tm, "%lu.%c", ARRAY_IDX + 1, 0).data : "•";
            GtkWidget *bullet = gtk_label_new(bullet_str);
            gtk_box_append(GTK_BOX(item), bullet);
            gtk_widget_add_css_class(bullet, "bullet");
            gtk_widget_set_valign(bullet, GTK_ALIGN_START);

            GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
            gtk_box_append(GTK_BOX(item), content);
            gtk_widget_add_css_class(bullet, "block");

            array_iter (c, &child->children) gtk_box_append(GTK_BOX(content), render_block(m, c));
        }

        result = w;
    } break;

    case MARKUP_AST_SEPARATOR: {
        Auto w = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_add_css_class(w, "block");
        gtk_widget_add_css_class(w, "frame");
        gtk_widget_add_css_class(w, "separator");
        result = w;
    } break;

    case MARKUP_AST_RAW_BLOCK: {
        Auto w = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_add_css_class(w, "block");
        gtk_widget_add_css_class(w, "frame");
        gtk_widget_add_css_class(w, "raw-block");
        array_iter (child, &node->children) gtk_box_append(GTK_BOX(w), render_block(m, child));
        result = w;
    } break;

    case MARKUP_AST_HEADER: {
        tmem_new(tm);

        Auto n = reinterpret_cast<MarkupAstHeader*>(node);
        GtkWidget *w = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_add_css_class(w, "header");
        gtk_widget_add_css_class(w, "frame");
        gtk_widget_add_css_class(w, "block");
        String css_class = astr_fmt(tm, "h%lu%c", n->size, 0);
        gtk_widget_add_css_class(w, css_class.data);

        GtkWidget *paragraph = render_block(m, array_get(&node->children, 0));
        gtk_box_append(GTK_BOX(w), paragraph);

        result = w;
    } break;

    default: badpath;
    }

    map_add(&m->ast_to_widget, node, result);
    return result;
}

static UiMarkupPosition *ui_markup_get_position_info (UiMarkup *m, Mem *mem, U64 idx) {
    Auto pos  = mem_new(mem, UiMarkupPosition);
    pos->idx  = idx;
    pos->text = m->text;
    pos->ast_path = markup_ast_path_for_idx(mem, m->ast, idx);
    array_init(&pos->widget_path, mem);

    array_iter (node, &pos->ast_path) {
        GtkWidget *w = map_get_ptr(&m->ast_to_widget, node);
        if (w) array_push(&pos->widget_path, w);
        if (node->tag == MARKUP_AST_PARAGRAPH) break; // Inline nodes don't have corresponding widgets.
    }

    return pos;
}

UiMarkup *ui_markup_new (Mem *mem, String text, MarkupAst *ast) {
    Auto m = mem_new(mem, UiMarkup);
    m->mem = mem;
    m->text = text;
    m->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(m->widget, "kronomi-markup");

    map_init_hc(&m->ast_to_widget, mem, 0, markup_ast_hash, +[](MarkupAst *a, MarkupAst *b){ return a == b; });

    m->ast = ast;

    array_iter (child, &m->ast->children) {
        GtkWidget *block = render_block(m, child);
        gtk_box_append(GTK_BOX(m->widget), block);
    }

    return m;
}

UiMarkup *ui_markup_new (Mem *mem, String text) {
    MarkupAst *ast = markup_parse(mem, text);
    return ui_markup_new(mem, text, ast);
}

static Void ui_markup_editor_render (UiMarkupEditor *e) {
    if (e->markup) gtk_widget_unparent(e->markup->widget);
    arena_pop_all(e->markup_arena);
    e->highlight = 0;

    String text = ui_text_view_get_text(&e->markup_arena->base, e->text_view);

    e->markup = ui_markup_new(&e->markup_arena->base, text);
    gtk_box_append(GTK_BOX(e->markup_scroll->box), e->markup->widget);
}

static Void ui_markup_editor_sync_scroll (UiMarkupEditor *e) {
    g_idle_add([](Void *data){
        tmem_new(tm);

        Auto e = static_cast<UiMarkupEditor*>(data);

        if (e->highlight) gtk_widget_remove_css_class(e->highlight, "highlight");

        UiMarkupPosition *info = ui_markup_get_position_info(e->markup, tm, e->cursor_idx);

        e->highlight = array_try_get_last(&info->widget_path);
        if (e->highlight) {
            if (gtk_widget_get_visible(e->text_view->widget->widget)) gtk_widget_add_css_class(e->highlight, "highlight");
            gtk_viewport_scroll_to(GTK_VIEWPORT(e->markup_scroll->viewport), e->highlight, 0);
        }

        return 0;
    }, e);
}

UiMarkupEditor *ui_markup_editor_new (Mem *mem, String initial_text, Void(*on_render)(UiMarkupEditor*)) {
    Auto e = mem_new(mem, UiMarkupEditor);

    e->on_render = on_render;
    e->text_view = ui_text_view_new(mem);
    ui_text_view_set_text(e->text_view, initial_text);

    e->markup_scroll = ui_scrollbox_new(mem, true);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(e->markup_scroll->widget), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    e->markup_arena = arena_new(mem, 512);

    //
    // listen
    //
    Auto controller = gtk_event_controller_legacy_new();
    gtk_event_controller_set_propagation_phase(controller, GTK_PHASE_CAPTURE);
    gtk_widget_add_controller(e->markup_scroll->box, controller);
    g_signal_connect(controller, "event", G_CALLBACK(+[](GtkEventControllerLegacy *, GdkEvent *, Void *data){
        Auto e = reinterpret_cast<UiMarkupEditor*>(data);
        return !e->preview_reactive;
    }), e);

    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(e->text_view->entry));
    Auto on_cursor_moved = +[](GtkTextBuffer *, GtkTextIter *iter, GtkTextMark *, Void *data){
        Auto e = static_cast<UiMarkupEditor*>(data);
        gint idx = gtk_text_iter_get_offset(iter);
        if (e->cursor_idx == idx) return;
        e->cursor_idx = idx;
        ui_markup_editor_sync_scroll(e);
    };
    g_signal_connect(buf, "mark-set", G_CALLBACK(on_cursor_moved), e);
    g_signal_connect(buf, "changed", G_CALLBACK(+[](GtkTextBuffer*, Void *data){
        Auto e = static_cast<UiMarkupEditor*>(data);
        if (e->timeout_id) g_source_remove(e->timeout_id);
        e->timeout_id = g_timeout_add(200, [](Void*data) {
            Auto e = static_cast<UiMarkupEditor*>(data);
            ui_markup_editor_render(e);
            ui_markup_editor_sync_scroll(e);
            if (e->on_render) e->on_render(e);
            e->timeout_id = 0;
            return 0;
        }, e);
    }), e);

    ui_markup_editor_render(e);
    ui_markup_editor_sync_scroll(e);
    if (e->on_render) e->on_render(e);

    return e;
}

UiMarkupEditor *ui_markup_editor_new (Mem *mem, String initial_text) {
    return ui_markup_editor_new(mem, initial_text, 0);
}

UiMarkupEditor2 *ui_markup_editor2_new (Mem *mem, String initial_text, CString text_title, CString preview_title) {
    Auto e = mem_new(mem, UiMarkupEditor2);

    e->ed = ui_markup_editor_new(mem, initial_text);

    e->entry_card = ui_card_new(mem);
    gtk_box_append(GTK_BOX(e->entry_card->body), e->ed->text_view->widget->widget);
    gtk_widget_add_css_class(e->entry_card->widget, "kronomi-entry-card");
    gtk_box_append(GTK_BOX(e->entry_card->left_header), gtk_label_new(text_title));

    e->markup_card = ui_card_new(mem);
    gtk_box_append(GTK_BOX(e->markup_card->body), e->ed->markup_scroll->widget);
    gtk_widget_add_css_class(e->markup_card->widget, "kronomi-preview-card");
    gtk_box_append(GTK_BOX(e->markup_card->left_header), gtk_label_new(preview_title));

    return e;
}
