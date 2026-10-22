/* rhunext.h: write rhun extensions in C or C++. One header, C99, the C library only.
 *
 * An extension is a program rhun starts and talks to over standard input and output, one text
 * line per message (the protocol: v1/v1.md in https://github.com/rhun-editor/extension-protocol).
 * This header does the talking: it reads what rhun sends, calls your handlers, frames your
 * requests and waits for their replies. Events that arrive while a request waits are delivered
 * after the handler returns, in order.
 *
 *     #define RHUNEXT_IMPLEMENTATION      // in exactly one .c or .cpp file
 *     #include "rhunext.h"
 *
 *     static void on_command(rx_ext *ext, const char *name, rx_doc doc, void *user) {
 *         if (strcmp(name, "hello") == 0) rx_toast(ext, "Hello from C");
 *     }
 *     int main(void) {
 *         rx_handlers h = {0};
 *         h.command = on_command;
 *         return rx_run("", &h, NULL);
 *     }
 *
 * Features (section 12 of the spec) are asked for by name in rx_run: "views" for pictures in
 * tabs of their own; "complete", "signature", "definition", "references", "symbols", "actions",
 * "rename" and "format" for language support ("views complete definition"). Protocol 1.1 adds
 * workspace edits as transactions and file operations ("workspace-edits file-operations":
 * rx_wedit_file_hashed, rx_wedit_create/rename/delete, rx_wedit_label, rx_wedit_confirm,
 * rx_wedit_finish_for for the grammar rhun agreed,
 * rx_wedit_from_ordered for LSP's ordered changes, rx_last_apply_error), several selections
 * ("selections": rx_select_many, rx_parse_selections), "reveal" (rx_reveal,
 * rx_show_locations), "diff" (rx_diff, rx_diff_close), "linked-editing" (the linked handler),
 * "query-stream" (rx_stream_part, rx_stream_progress), "provider-scope" (rx_provides),
 * "complete-items" (rx_list_item_extra, complete_resolve), "signature-list" (signatures),
 * "navigation" (declaration, type_definition, implementation), "prepare-rename" (prepare_rename),
 * "partial-format" (range_format, type_format), "code-actions" (code_actions with
 * rx_code_action, code_action_resolve), "hover-query" (hover), "selection-ranges"
 * (selection_ranges), "hierarchy" (call_hierarchy, calls_in, calls_out, type_hierarchy, supertypes,
 * subtypes), "workspace-symbols" (workspace_symbols), "document-links" (document_links) and
 * "file-diagnostics" (rx_file_diagnostics, rx_reset_diagnostics), "tests" for a test controller
 * (rx_test_item, rx_test_update, rx_test_exec and the tests handlers), and "terminals tasks" for
 * programs in terminal tabs and published tasks (rx_open_terminal, rx_set_tasks).
 * Strings in and out are UTF-8; positions are 1-based lines and 1-based byte columns.
 * The same source builds for wasm32-wasip1 (clang --target=wasm32-wasip1 with a WASI sysroot), one
 * module rhun runs itself on every system: run.wasm32-wasip1 = bin/NAME.wasm in the manifest.
 * MIT license. */
#ifndef RHUNEXT_H
#define RHUNEXT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int64_t rx_doc;                         /* a document id; 0 is none */
typedef struct { int line, col; } rx_pos;       /* 1-based line, 1-based byte column */
typedef struct { rx_pos start, end; } rx_range; /* half-open */
typedef struct rx_ext rx_ext;
struct rx_commit_message_ask_s;                  /* source control: rx_commit_message_ask */
struct rx_quickdiff_ask_s;                       /* and rx_quickdiff_ask */
struct rx_debug_resolve_ask_s;                   /* debuggers: rx_debug_resolve_ask */
struct rx_debug_config_s;                        /* and rx_debug_config */
struct rx_invocation_s;                          /* command-args: rx_invocation */
struct rx_call_ask_s;                            /* and rx_call_ask */

/* bytes that grow: pictures, answers, anything built piece by piece */
typedef struct { char *data; size_t len, cap; } rx_buf;

/* an event, 0 NAME FIELDS: argv the fields after the name, rest the line after the name (a path
 * may hold spaces), body for edits and action */
typedef struct {
    const char *name;
    int argc;
    char **argv;
    const char *rest;
    const unsigned char *body;
    size_t body_len;
} rx_event;

/* a view's event (12a): kind is size, metrics, hover, down, drag, up, click, leave, wheel, closed;
 * with "view-keys" also key, text, paste, selectall, dblclick and focus */
typedef struct {
    const char *kind;
    double x, y, dy;            /* the pointer; dy for wheel */
    double width, height;       /* size */
    double advance[4], line_height[4];  /* metrics: small, normal, large, mono */
    const char *mods;           /* view-keys: "-" or ctrl+shift+alt+super (pointer events, key), else "" */
    const char *key;            /* key: its name ("Return", "Left", "F5", "a") */
    const unsigned char *text;  /* text (typed) and paste (the clipboard's), text_len bytes */
    size_t text_len;
    int on;                     /* focus */
} rx_view_event;

/* what rhun asks for (12b to 12e), an rx_ask: kind is typed, trigger (ch is the character), invoked,
 * incomplete or update */
typedef struct {
    int64_t id;                 /* rhun's id for it: a "cancel" event names it */
    rx_doc doc;
    int64_t version;            /* positions in the answer are in the document at this version */
    rx_pos at;
    const char *kind;
    const char *ch;
} rx_ask;

/* code actions (12f): for the selection, or an empty range at the cursor */
typedef struct {
    int64_t id;
    rx_doc doc;
    int64_t version;
    rx_range range;
} rx_actions_ask;

/* rename (12g): the symbol at `at` becomes name (no spaces) */
typedef struct {
    int64_t id;
    rx_doc doc;
    int64_t version;
    rx_pos at;
    const char *name;
} rx_rename_ask;

/* format (12h), with the document's own settings: spaces 1 indents with spaces, 0 with tabs */
typedef struct {
    int64_t id;
    rx_doc doc;
    int64_t version;
    int tab_width, spaces;
} rx_format_ask;

/* Source actions before saving (1.1). A cancel event names id. Serial handlers must bound
 * their work; custom asynchronous dispatchers must revoke that invocation on cancellation. */
typedef struct {
    int64_t id;
    rx_doc doc;
    int64_t version;
    const char *kind;
} rx_save_actions_ask;

/* Protocol 1.1, complete-items: rhun resolves the selected item with its resolve data (data[len]
 * is 0). Selecting another item withdraws it (a cancel event names id). */
typedef struct {
    int64_t id;
    rx_doc doc;
    int64_t version;            /* the list's: the resolution's edits are in the document then */
    const unsigned char *data;
    size_t len;
} rx_resolve_ask;

/* Protocol 1.1, partial-format: format range (range-format), or the document after ch was typed
 * ("newline" for Enter) with the cursor at `at`, after it (type-format); with the document's
 * settings in force: spaces 1 indents with spaces, trim and final_newline are its
 * trim_trailing_whitespace and final_newline (1 yes). rhun applies a type-format edit only while the
 * document is still at version. */
typedef struct {
    int64_t id;
    rx_doc doc;
    int64_t version;
    rx_range range;
    int tab_width, spaces, trim, final_newline;
} rx_range_format_ask;
typedef struct {
    int64_t id;
    rx_doc doc;
    int64_t version;
    rx_pos at;
    const char *ch;
    int tab_width, spaces, trim, final_newline;
} rx_type_format_ask;

/* Protocol 1.1, code-actions: for range (the selection; without one the diagnostic at the cursor, or
 * the cursor); trigger "invoked" (the user asked) or "auto" (the lightbulb); kinds the hierarchical
 * kinds wanted, comma separated, "" for any (rx_kind_wanted); diagnostics the comma-separated ids of
 * this extension's diagnostics in the range, "" for none. */
typedef struct {
    int64_t id;
    rx_doc doc;
    int64_t version;
    rx_range range;
    const char *trigger, *kinds, *diagnostics;
} rx_code_actions_ask;

/* Protocol 1.1, query-stream: one of rhun's pending requests, handed to the *_stream handlers for
 * rx_stream_part and rx_stream_progress. It is cancelled when rhun withdraws the request (a cancel
 * event), answers a part cancelled or toobig, or the handler returns. */
typedef struct { rx_ext *ext; int64_t id; } rx_stream;

/* answers to 12d to 12h, built with rx_location, rx_symbol, rx_action and rx_wedit_file (below);
 * the fields are the builders' own */
typedef struct { rx_buf recs; int n, full; } rx_locations;
typedef struct { rx_buf recs; int n, full; } rx_symbols;
typedef struct { rx_buf recs; int n, full; int caps; } rx_actions;   /* caps: set by rx_run */
typedef struct {
    rx_buf body, group, target;     /* the files so far; the open file's edits and PATH */
    int64_t version;
    int files, edits, group_edits, open;
    const char *fail;               /* NULL, or the code it is refused with (toobig, refused, */
    char why[256];                  /* syntax, unknown) and why, one line */
    /* protocol 1.1: the creates, renames and deletes so far, where hashed PREs start in body (one
     * offset per hashed group: a rhun without workspace-edits refuses them), the label, confirm,
     * and the open file's hash */
    rx_buf ops[3], hashes, label;
    int nops, confirm, hashed;
    uint64_t hash;
} rx_wedit;
/* Protocol 1.1, code-actions: the answer, built with rx_code_action; kinds (set for you in the
 * code_actions handler: the kinds asked, NULL or "" for any) leaves out actions of other kinds */
typedef struct { rx_buf recs; int n, full; const char *kinds; int caps; } rx_code_actions;   /* caps: set by rx_run */
/* code-action-resolve's answer: the edit (none while it names no file and no file operation) and the
 * command to run after it (rx_command_ref(&out->command, ...); none: the action's own runs) */
typedef struct { rx_wedit edit; rx_buf command; int caps; } rx_resolved;   /* caps: set by rx_run */

struct rx_view_open;            /* retained views, below */
struct rx_retained_event;
struct rx_children_ask;
struct rx_patch_s;
/* a virtual document's text (feature uris): lang a grammar name or "", title or "", text_len bytes
 * of UTF-8 (at most 4 MiB); the pointers stay the handler's */
typedef struct {
    const char *lang, *title, *text;
    size_t text_len;
} rx_content;

/* protocol 1.1, agents, proposals and tools (v1.1/v1.1.md, "Agents, proposals and tools") */
typedef struct {
    const char *id, *title, *model, *cwd;   /* id: 1 to 128 visible ASCII bytes; cwd "" for any project */
    const char *state;                       /* idle, working, waiting or error; NULL is idle */
    int64_t updated;                         /* Unix seconds of its last change */
} rx_session;
typedef struct { rx_buf recs; int64_t total; int n; size_t size; } rx_transcript;
typedef struct {
    int64_t proposal, index;        /* index from 1; 0 for done */
    const char *state;              /* accepted, rejected, stale, partial or done */
    int64_t accepted, hunks, rejected;  /* partial: hunks accepted of hunks; done: files */
} rx_proposal_event;
typedef struct { char *name, *title, *description, *input; int readonly; } rx_tool;

/* linked editing (1.1): rhun asks for the ranges that change with the word at `at` */
typedef struct rx_linked_ask {
    int64_t id;
    rx_doc doc;
    int64_t version;            /* the ranges are in the document at this version */
    rx_pos at;
} rx_linked_ask;
/* the answer, built with rx_linked_range and rx_linked_word; the fields are the builder's own */
typedef struct rx_linked {
    rx_buf ranges;
    int n, has_word;
    char word[96];
} rx_linked;

/* Notebooks (feature "notebooks"). nb-open: the file's path and bytes (len 0 for a new file);
 * nb-save: the path and the notebook body; nb-run: run source for (nb, cell, run). */
typedef struct rx_nb_open { int64_t id, nb; const char *path; const unsigned char *data; size_t len; } rx_nb_open;
typedef struct rx_nb_save { int64_t id, nb; const char *path; const unsigned char *body; size_t len; } rx_nb_save;
typedef struct rx_nb_run { int64_t nb, cell, run; const char *lang; const unsigned char *source; size_t len; } rx_nb_run;

/* Tests (1.1, feature "tests"; v1.1.md "Feature: tests"): items under one parent, built with
 * rx_test_item; a run rhun asks for (profile run, debug or coverage; no items: everything);
 * results of a run, built with rx_test_result and the extras after it; coverage, built with
 * rx_coverage_file and rx_coverage_lines. Zero them to start; the fields are the builders' own. */
typedef struct rx_test_items { rx_buf recs; int n; const char *fail; char why[256]; } rx_test_items;
typedef struct rx_test_run { int64_t run; const char *profile; int nitems; const char *const *items; } rx_test_run;
typedef struct rx_test_results {
    rx_buf recs, extra, item;
    int n, open;
    const char *state;
    long long ms;
    const char *fail;
    char why[256];
} rx_test_results;
typedef struct rx_test_coverage { rx_buf recs, ranges, path; int files, nranges, open; } rx_test_coverage;

/* Protocol 1.1, hierarchy: calls-in, calls-out, supertypes and subtypes hand back an item's data
 * (data[len] is 0) */
typedef struct {
    int64_t id;
    const unsigned char *data;
    size_t len;
} rx_data_ask;
/* selection-ranges: each selection's cursor, n of them (1 to 64) */
typedef struct {
    int64_t id;
    rx_doc doc;
    int64_t version;
    int n;
    const rx_pos *positions;
} rx_selection_ask;
/* workspace-symbols: the query (one line, at most 256 bytes, maybe empty; query[len] is 0) and
 * max, a hint of how many symbols are wanted */
typedef struct {
    int64_t id;
    const char *query;
    size_t len;
    int max;
} rx_workspace_symbols_ask;
/* document-links: the whole document at version */
typedef struct {
    int64_t id;
    rx_doc doc;
    int64_t version;
} rx_doc_ask;

/* Editor presentation (protocol 1.1; v1.1.md "Feature: decorations", "Feature: diagnostic-tags",
 * "Presentation providers"). rx_decoration_type registers or restyles this extension's decoration
 * type (name 1-40 of [a-z0-9_-]; body "KEY ARGS" lines: color, background, line-background, border,
 * underline, strike, faded, before, after, width, gutter, scrollbar, grow, priority). rx_decorate
 * replaces this extension's ranges of a type on doc whose first line is in from..to (0 0: all);
 * version may be one rhun still keeps the edits since. rx_semantic_legend registers a legend: its
 * token types and modifiers, space separated, each NAME or NAME=EFFECT. Each returns 0, or -1 with
 * rx_error. rx_visible reads a "0 visible DOC L1 L2" event (1, or 0 for another event).
 * rx_tagged_severity writes into out a diagnostics line's SEVERITY with its tags when the extension
 * agreed diagnostic-tags ("hint+unnecessary"), the severity alone otherwise. */
typedef struct {
    rx_range r;
    const char *before, *after, *tip;   /* virtual text (one line, 200 bytes) and a tip; NULL: none */
    const char *command;                /* a command reference's NAME run by a click on its text, or NULL */
    const void *args;
    size_t args_len;
} rx_decoration;
int rx_decoration_type(rx_ext *ext, const char *name, const char *body);
int rx_decorate(rx_ext *ext, rx_doc doc, int64_t version, const char *name, int from, int to,
                const rx_decoration *ranges, int n);
int rx_semantic_legend(rx_ext *ext, const char *name, const char *types, const char *modifiers);
int rx_visible(const rx_event *ev, rx_doc *doc, int *first, int *last);
const char *rx_tagged_severity(rx_ext *ext, const char *severity, int unnecessary, int deprecated, char out[48]);
/* what rhun asks providers about: lines first..last (inlay-hints, code-lens, colors); semantic
 * tokens (prev the last result's id, "" for none; range 1 with first..last for a huge document); a
 * color's new value (color-edit, rgba 0xRRGGBBAA) */
typedef struct { int64_t id; rx_doc doc; int64_t version; int first, last; } rx_lines_ask;
typedef struct { int64_t id; rx_doc doc; int64_t version; const char *prev; int range, first, last; } rx_tokens_ask;
typedef struct { int64_t id; rx_doc doc; int64_t version; rx_range r; uint32_t rgba; } rx_color_ask;
/* an answer's records, built with the functions below; rx_pres_finish appends "N" LF and them */
typedef struct { rx_buf recs; int n, full; } rx_pres;
void rx_folding_range(rx_pres *l, int start, int end, const char *kind, const char *text);
typedef struct {
    const char *label, *tip;            /* label one line (a hint's parts: 120 bytes in all); tip NULL */
    const char *command;                /* a click runs it (NAME and args), or NULL */
    const void *args;
    size_t args_len;
    int has_goto;                       /* or a click opens goto_path ("" for this document) at goto_at */
    const char *goto_path;
    rx_pos goto_at;
} rx_hint_part;
void rx_inlay_hint(rx_pres *l, rx_pos at, int before, const rx_hint_part *parts, int nparts,
                   const rx_range *edit_ranges, const char *const *edit_texts, int nedits);
void rx_code_lens(rx_pres *l, int line, const char *title, const char *tip, const char *command, const void *args,
                  size_t args_len);
void rx_highlight(rx_pres *l, rx_range r, const char *kind);   /* kind text, read or write */
void rx_document_color(rx_pres *l, rx_range r, uint32_t rgba);
void rx_inline_item(rx_pres *l, rx_range r, const char *text, const char *command, const void *args,
                    size_t args_len);
/* rx_inline_item with edits made when the item is taken (as Go's InlineItem.Edits and Rust's
 * InlineItem::edits): nedits ranges and texts (at most 16; a backwards range ends the list) */
void rx_inline_item_edits(rx_pres *l, rx_range r, const char *text, const char *command, const void *args,
                          size_t args_len, const rx_range *edit_ranges, const char *const *edit_texts, int nedits);
void rx_pres_finish(rx_pres *l, rx_buf *out);
/* semantic tokens: the whole answer into out: data n ints (five per token, LSP's layout with byte
 * columns; MODS 64 bits, bit i the legend's modifier i), or a delta of nedits edits, each then
 * appended with rx_semantic_edit */
void rx_semantic_full(rx_buf *out, const char *result, const char *legend, const uint64_t *data, size_t n, int range);
void rx_semantic_delta(rx_buf *out, const char *result, const char *legend, int nedits);
void rx_semantic_edit(rx_buf *out, uint32_t start, uint32_t del, const uint64_t *data, size_t n);
/* the answers, built with rx_selection_group, rx_hierarchy_add, rx_call_add, rx_workspace_symbol
 * and rx_document_link (below); the fields are the builders' own (at and npos, the request's positions, and
 * roots are set for you in the handlers) */
typedef struct { rx_buf ranges; int counts[64], n; rx_pos at[64]; int npos; } rx_selections;
typedef struct { rx_buf recs; int n, full, roots; } rx_hierarchy;
typedef struct { rx_buf recs; int n, full; } rx_calls;
typedef struct { rx_buf recs; int n, full; } rx_workspace_symbols;
typedef struct { rx_buf recs; int n, full; } rx_links;
/* an item of a call or type hierarchy: kind a symbol kind word (12e; NULL is function); name one
 * line (an empty one leaves the item out) and detail one line or NULL, each cut at 1000 bytes; the
 * file by its absolute path, or with path NULL the open document doc; range the whole symbol and
 * selection its name inside it (another is range); data, at most 16 KiB (else the item is left
 * out), comes back verbatim when rhun asks about the item. */
typedef struct {
    const char *kind, *name, *detail, *path;
    rx_doc doc;
    rx_range range, selection;
    const void *data;
    size_t data_len;
} rx_hierarchy_item;

/* Protocol 1.1, file-diagnostics: a place related to a diagnostic (the file by its absolute path,
 * or with path NULL the open document doc; message one line) */
typedef struct {
    const char *path;
    rx_doc doc;
    rx_range range;
    const char *message;
} rx_related;
/* a diagnostic: severity error warning info hint (NULL is error; another leaves it out); message 1
 * to 4000 bytes (cut at a character; NULL or "" leaves it out), line feeds kept; id a token of
 * [A-Za-z0-9_.:-], at most 64 bytes, unique in the set, handed back in code-actions requests; code
 * and source one line, cut at 100 bytes (the problem shows source(code)); href an http(s) link of at
 * most 1000 bytes; the tags unnecessary and deprecated; at most 16 related places. NULL and 0 leave a
 * record out, as do an id, a link or a related place rhun would refuse. */
typedef struct {
    rx_range range;
    const char *severity, *message, *id, *code, *source, *href;
    int unnecessary, deprecated;
    const rx_related *related;
    int nrelated;
} rx_diagnostic;

typedef struct {
    void (*ready)(rx_ext *ext, const char *project, void *user);    /* project "" without one */
    void (*command)(rx_ext *ext, const char *name, rx_doc doc, void *user);   /* name without "NAME." */
    void (*event)(rx_ext *ext, const rx_event *ev, void *user);     /* every event, first */
    void (*view)(rx_ext *ext, int64_t view, const rx_view_event *ev, void *user);
    /* append the answer's body to out (rx_list_finish, rx_signature_answer); return 1 to answer,
     * 0 for "unknown". An empty signature body hides the help. */
    int (*complete)(rx_ext *ext, const rx_ask *req, rx_buf *out, void *user);
    int (*signature)(rx_ext *ext, const rx_ask *req, rx_buf *out, void *user);
    void (*bye)(rx_ext *ext, void *user);
    /* Definition, references, symbols (12d, 12e): add to out (rx_location, rx_symbol); return 1 to
     * answer (none says there is none), 0 for "unknown". */
    int (*definition)(rx_ext *ext, const rx_ask *req, rx_locations *out, void *user);
    int (*references)(rx_ext *ext, const rx_ask *req, rx_locations *out, void *user);
    int (*symbols)(rx_ext *ext, const rx_ask *req, rx_symbols *out, void *user);
    /* Code actions (12f): add to out (rx_action) and return the same; the user choosing one that
     * has data sends it to action (data[len] is 0), after its edit. */
    int (*actions)(rx_ext *ext, const rx_actions_ask *req, rx_actions *out, void *user);
    void (*action)(rx_ext *ext, const unsigned char *data, size_t len, void *user);
    /* Rename, format (12g, 12h): build the workspace edit in out (rx_wedit_file, rx_wedit_edit) and
     * return the same; an empty one changes nothing. Nothing can be renamed there: return
     * rx_refuse(out, why). An edit past the limits is answered toobig. */
    int (*rename)(rx_ext *ext, const rx_rename_ask *req, rx_wedit *out, void *user);
    int (*format)(rx_ext *ext, const rx_format_ask *req, rx_wedit *out, void *user);
    int (*save_actions)(rx_ext *ext, const rx_save_actions_ask *req, rx_wedit *out, void *user);
    /* Commit messages (feature "commit-message", [provides] commit_message = yes): append the
     * message to out and return 1; append why not and return -1 (the user sees it); 0 for
     * "unknown". req's strings live until it returns. */
    int (*commit_message)(rx_ext *ext, const struct rx_commit_message_ask_s *req, rx_buf *out, void *user);
    /* Quick-diff bases (feature "quickdiff", [provides] quickdiff = PATTERNS): append the file's
     * base text to out (nothing: leave the gutter marks to git) and return 1; 0 for "unknown". */
    int (*quickdiff)(rx_ext *ext, const struct rx_quickdiff_ask_s *req, rx_buf *out, void *user);
    /* Retained views (1.1, feature "view-tree"): a declared view's instance exists (fill it with
     * rx_view_patch), and what the user did in one. */
    void (*view_open)(rx_ext *ext, const struct rx_view_open *open, void *user);
    void (*view_event)(rx_ext *ext, int64_t view, const struct rx_retained_event *ev, void *user);
    /* rhun asks for a node's children (vkids, have -1) or more of them (vmore, have: how many it
     * has): build the patch in out (a kids or add op for the node) and return 1; 0 for "unknown";
     * set out->fail to a reason to refuse */
    int (*view_children)(rx_ext *ext, const struct rx_children_ask *req, struct rx_patch_s *out, void *user);
    /* Resources group, feature uris: fill out for a URI of one of the schemes in [provides]
     * schemes and return 1, or return 0 for "nofile"; content_closed: the last tab showing it
     * closed. */
    int (*content)(rx_ext *ext, const char *uri, rx_content *out, void *user);
    void (*content_closed)(rx_ext *ext, const char *uri, void *user);
    /* Feature agents: a session's messages from `from` (rx_transcript_add); 1 answers, 0 "unknown". */
    int (*agent_transcript)(rx_ext *ext, const char *sid, int64_t from, rx_transcript *out, void *user);
    /* Feature edit-review: what became of a proposal's file, or of the proposal. */
    void (*proposal)(rx_ext *ext, const rx_proposal_event *ev, void *user);
    /* Feature tools: a call of one of its [tools] (name without OWNER/NAME., caller OWNER/NAME):
     * the output in out and 1, or the error's "CODE TEXT" in out and -1; 0 is "unknown". */
    int (*tool)(rx_ext *ext, const char *name, const char *caller, const unsigned char *input, size_t len,
                rx_buf *out, void *user);
    /* Feature agent-prompts: a prompt for a session (sid "" starts one): its id in out and 1, or
     * "CODE TEXT" and -1. agent_stop: the user's Stop. */
    int (*agent_prompt)(rx_ext *ext, const char *sid, const char *text, size_t len, rx_buf *out, void *user);
    void (*agent_stop)(rx_ext *ext, const char *sid, void *user);
    /* Linked editing (1.1, "linked-editing", [provides] linked): add the ranges that change
     * together to out (rx_linked_range, rx_linked_word) and return 1 (none: nothing to link). */
    int (*linked)(rx_ext *ext, const struct rx_linked_ask *req, struct rx_linked *out, void *user);
    /* Notebooks (feature "notebooks", v1.1): nb_open appends a notebook body to out (rx_nb_begin,
     * rx_nb_cell, rx_nb_output, rx_nb_item) and returns NULL, or returns why it cannot (shown in
     * the tab); nb_save reads req->body (rx_nb_read_begin...) and appends the file's bytes to out,
     * or returns why not. nb_run starts a run (answer with rx_nb_state, rx_nb_output_send,
     * rx_nb_stream); nb_event hears "interrupt", "restart", "kernel" and "closed". */
    const char *(*nb_open)(rx_ext *ext, const rx_nb_open *req, rx_buf *out, void *user);
    const char *(*nb_save)(rx_ext *ext, const rx_nb_save *req, rx_buf *out, void *user);
    void (*nb_run)(rx_ext *ext, const rx_nb_run *run, void *user);
    void (*nb_event)(rx_ext *ext, const char *what, int64_t nb, void *user);
    /* Notebooks: a kernel started with rx_nb_exec ended; code its exit status (128 + N after
     * signal N), or -1 when rhun ended it. */
    void (*nb_exec_exit)(rx_ext *ext, int64_t exec, int code, void *user);
    /* Notebooks: a cell document it was told about belongs to notebook nb as cell `cell`, at index
     * among all its cells; nb_cells lists the cell documents it knows in order (n of them). */
    void (*nb_cell)(rx_ext *ext, rx_doc doc, int64_t nb, int64_t cell, int index, void *user);
    void (*nb_cells)(rx_ext *ext, int64_t nb, const rx_doc *docs, int n, void *user);
    /* Protocol 1.1. When set, the *_stream members answer complete, definition and references in
     * place of the ones above; stream sends parts and progress (query-stream). complete_resolve
     * (complete-items) appends the resolution (rx_resolve_answer) to out; signatures
     * (signature-list: rhun sends it in place of signature) appends rx_signatures_answer. Each
     * returns 1 to answer, 0 for "unknown". */
    int (*complete_stream)(rx_ext *ext, const rx_ask *req, rx_stream *stream, rx_buf *out, void *user);
    int (*definition_stream)(rx_ext *ext, const rx_ask *req, rx_stream *stream, rx_locations *out, void *user);
    int (*references_stream)(rx_ext *ext, const rx_ask *req, rx_stream *stream, rx_locations *out, void *user);
    int (*complete_resolve)(rx_ext *ext, const rx_resolve_ask *req, rx_buf *out, void *user);
    int (*signatures)(rx_ext *ext, const rx_ask *req, rx_buf *out, void *user);
    /* Protocol 1.1, each asked only with its feature agreed, and returning 1 to answer, 0 for
     * "unknown". navigation: declaration, type_definition and implementation answer as
     * definition_stream does (locations; parts through stream with query-stream). prepare-rename:
     * prepare_rename appends rx_prepare_rename_answer to out, or nothing for the word at the
     * cursor; nothing can be renamed there: return rx_refuse_answer(out, why) (shown to the user).
     * partial-format: range_format and type_format build a workspace edit for that document only,
     * as format does. code-actions (in place of actions): code_actions adds rx_code_action records;
     * code_action_resolve gets the chosen action's resolve data and builds its edit in out->edit
     * and its command in out->command; nothing comes: return rx_refuse(&out->edit, why). */
    int (*declaration)(rx_ext *ext, const rx_ask *req, rx_stream *stream, rx_locations *out, void *user);
    int (*type_definition)(rx_ext *ext, const rx_ask *req, rx_stream *stream, rx_locations *out, void *user);
    int (*implementation)(rx_ext *ext, const rx_ask *req, rx_stream *stream, rx_locations *out, void *user);
    int (*prepare_rename)(rx_ext *ext, const rx_ask *req, rx_buf *out, void *user);
    int (*range_format)(rx_ext *ext, const rx_range_format_ask *req, rx_wedit *out, void *user);
    int (*type_format)(rx_ext *ext, const rx_type_format_ask *req, rx_wedit *out, void *user);
    int (*code_actions)(rx_ext *ext, const rx_code_actions_ask *req, rx_code_actions *out, void *user);
    int (*code_action_resolve)(rx_ext *ext, const rx_resolve_ask *req, rx_resolved *out, void *user);
    /* Notifications (feature "notify"): button N (from 1) without a command was pressed, or (0)
     * the card was closed by the user, a timeout or Clear All. */
    void (*note_event)(rx_ext *ext, int64_t note, int pressed, void *user);
    /* Progress (feature "progress"): the user pressed Cancel; rhun removes it 10 s later unless
     * rx_progress_end comes first. */
    void (*progress_cancel)(rx_ext *ext, int64_t progress, void *user);
    /* Quick input (feature "quick-input"): "0 quick Q WHAT SEQ ...": what is value, active,
     * accept, back, button or hide; argv the fields after WHAT (SEQ first; accept: SEQ N; button:
     * SEQ KEY); body the text, the item id, or for accept the N ids each ending in LF, then the
     * field's text. */
    void (*quick_event)(rx_ext *ext, int64_t q, const char *what, int argc, char **argv,
                        const unsigned char *body, size_t body_len, void *user);
    /* Protocol 1.1, feature "command-args": every invocation of one of its commands with where it
     * came from and its body; when set it is called instead of command. */
    void (*invoke)(rx_ext *ext, const struct rx_invocation_s *inv, void *user);
    /* Another extension runs a command declared callable ([command-options] NAME.callable = yes):
     * append the result (at most 1 MiB) to out and return 1; append why not and return -1, with
     * *code the error code (default "refused"); 0 for "unknown". A cancel event names req->id. */
    int (*call)(rx_ext *ext, const struct rx_call_ask_s *req, rx_buf *out, const char **code, void *user);
    /* Resources group, feature urls: "0 link ID LEN", a link made by rx_link (request ID) reached
     * rhun; link is the whole link with the path and query the browser added (len bytes, NUL
     * after them). */
    void (*link)(rx_ext *ext, int64_t id, const char *link, size_t len, void *user);
    /* Tests (1.1, feature "tests"; types below). resolve_tests: parent's children ("" the roots)
     * in out, return 1 (rx_test_refuse(out, why) shows why on the row instead), 0 for "unknown".
     * run_tests: start the run and return NULL, or "CODE TEXT" to refuse it ("busy ..."); report
     * with rx_test_update and end with rx_test_end, here or from later handlers. cancel_tests: the
     * user's Cancel (stop, then rx_test_end). test_out and test_exit: a test_exec program's output
     * (err 1: its standard error), as read, and its end: code "0", "1", "signal N" or "killed". */
    int (*resolve_tests)(rx_ext *ext, const char *parent, struct rx_test_items *out, void *user);
    const char *(*run_tests)(rx_ext *ext, const struct rx_test_run *run, void *user);
    void (*cancel_tests)(rx_ext *ext, int64_t run, void *user);
    void (*test_out)(rx_ext *ext, int64_t exec, int err, const unsigned char *data, size_t len, void *user);
    void (*test_exit)(rx_ext *ext, int64_t exec, const char *code, void *user);
    /* Protocol 1.1, each asked only with its feature agreed, and returning 1 to answer, 0 for
     * "unknown". hover-query: hover appends rx_hover_answer to out (req->kind is pointer or
     * invoked), nothing to say nothing. selection-ranges: selection_ranges calls rx_selection_group
     * once for each of req->positions, in order. hierarchy: call_hierarchy and type_hierarchy add
     * the roots at the cursor (rx_hierarchy_add); calls_in and calls_out add calls (rx_call_add),
     * supertypes and subtypes items, of the item whose data req holds, with parts through stream
     * (query-stream). workspace-symbols: workspace_symbols adds rx_workspace_symbol records, with
     * parts through stream; a new query withdraws the request. document-links: document_links
     * adds rx_document_link records. */
    int (*hover)(rx_ext *ext, const rx_ask *req, rx_buf *out, void *user);
    int (*selection_ranges)(rx_ext *ext, const rx_selection_ask *req, rx_selections *out, void *user);
    int (*call_hierarchy)(rx_ext *ext, const rx_ask *req, rx_hierarchy *out, void *user);
    int (*calls_in)(rx_ext *ext, const rx_data_ask *req, rx_stream *stream, rx_calls *out, void *user);
    int (*calls_out)(rx_ext *ext, const rx_data_ask *req, rx_stream *stream, rx_calls *out, void *user);
    int (*type_hierarchy)(rx_ext *ext, const rx_ask *req, rx_hierarchy *out, void *user);
    int (*supertypes)(rx_ext *ext, const rx_data_ask *req, rx_stream *stream, rx_hierarchy *out, void *user);
    int (*subtypes)(rx_ext *ext, const rx_data_ask *req, rx_stream *stream, rx_hierarchy *out, void *user);
    int (*workspace_symbols)(rx_ext *ext, const rx_workspace_symbols_ask *req, rx_stream *stream,
                             rx_workspace_symbols *out, void *user);
    int (*document_links)(rx_ext *ext, const rx_doc_ask *req, rx_links *out, void *user);
    /* Copy or cut in a picture with the keyboard (feature "view-keys"): append the clipboard's
     * text to out and return 1 (nothing: the clipboard stays) */
    int (*view_copy)(rx_ext *ext, int64_t view, int cut, rx_buf *out, void *user);
    /* Custom editors (feature "custom-editors", v1.1): editor_open (model "text" or "owned",
     * view the 12a view rhun opened: draw it) returns NULL when ready, else why (the file opens
     * as text); rx_editor_source(ext) says, during it, whether rhun opens the file ("file") or
     * unsaved changes it kept ("backup": read them with rx_editor_read, and the editor starts
     * dirty); editor_save (kind "save", "saveas" or "backup") appends part `part` of an owned model's bytes to out (at most 4 MB;
     * snapshot at part 0), sets *more when another follows, returns NULL or why; editor_cmd hears
     * "undo", "redo", "revert" (NULL or why); editor_event "changed", "closed", and "path" with
     * the new path. */
    const char *(*editor_open)(rx_ext *ext, rx_doc cdoc, int64_t view, const char *model, const char *path, void *user);
    const char *(*editor_save)(rx_ext *ext, rx_doc cdoc, const char *kind, int64_t part, rx_buf *out, int *more, void *user);
    const char *(*editor_cmd)(rx_ext *ext, rx_doc cdoc, const char *what, void *user);
    void (*editor_event)(rx_ext *ext, rx_doc cdoc, const char *what, const char *path, void *user);
    /* Editor presentation (protocol 1.1), each asked only with its feature agreed and returning 1
     * to answer, 0 for "unknown": folding_ranges (folding-ranges) adds rx_folding_range records;
     * semantic_tokens (semantic-tokens) writes the answer with rx_semantic_full or rx_semantic_delta
     * (nothing to say nothing); inlay_hints (inlay-hints), code_lenses (code-lens), highlights
     * (document-highlights: req->at is the caret), colors (document-colors: rx_document_color) and inline_complete
     * (inline-complete: req->kind typed or invoked) add their records; color_edit (document-colors)
     * builds the workspace edit that writes req's color. */
    int (*folding_ranges)(rx_ext *ext, const rx_doc_ask *req, rx_pres *out, void *user);
    int (*semantic_tokens)(rx_ext *ext, const rx_tokens_ask *req, rx_buf *out, void *user);
    int (*inlay_hints)(rx_ext *ext, const rx_lines_ask *req, rx_pres *out, void *user);
    int (*code_lenses)(rx_ext *ext, const rx_lines_ask *req, rx_pres *out, void *user);
    int (*highlights)(rx_ext *ext, const rx_ask *req, rx_pres *out, void *user);
    int (*colors)(rx_ext *ext, const rx_lines_ask *req, rx_pres *out, void *user);
    int (*color_edit)(rx_ext *ext, const rx_color_ask *req, rx_wedit *out, void *user);
    int (*inline_complete)(rx_ext *ext, const rx_ask *req, rx_pres *out, void *user);
    /* debug-resolve (feature "debug", a debugger with resolve = yes): 0 launches req's configuration
     * unchanged; 1 launches *out (the complete configuration) instead; -1 refuses with why's text;
     * -2 cancels quietly. */
    int (*debug_resolve)(rx_ext *ext, const struct rx_debug_resolve_ask_s *req, struct rx_debug_config_s *out,
                         rx_buf *why, void *user);
} rx_handlers;

/* Retained views (protocol 1.1, feature "view-tree"): views declared in the manifest's [views]
 * (KEY.title, KEY.place = sidebar|panel|side, KEY.kind = tree, KEY.open = yes) whose content rhun
 * keeps. A declared view's instance: "0 view VIEW open KIND PLACE NAME". */
typedef struct rx_view_open {
    int64_t view;
    const char *kind;           /* tree */
    const char *place;          /* sidebar, panel, side or tab */
    const char *name;           /* the manifest key, or "-" */
} rx_view_open;
/* what the user did: kind visible, focus, select, activate, expand, collapse, check, action,
 * discarded, closed (or another word, with its fields in argv); on for visible, focus and check;
 * rev the content the user acted on; id the node (select: the first one; body holds them all,
 * each ending in LF); key an action's key or what was discarded */
typedef struct rx_retained_event {
    const char *kind;
    int on;
    int64_t rev;
    const unsigned char *id;
    size_t id_len;
    const char *key;
    int argc;
    char **argv;
    const unsigned char *body;
    size_t body_len;
} rx_retained_event;
/* A patch: ops applied in order, whole or not at all. Build node records into an rx_buf with
 * rx_node_rec (their extra records with the rx_x_* builders first), then rx_patch_kids. */
typedef struct rx_patch_s { rx_buf body; int n; long nodes; const char *fail; } rx_patch;
typedef struct rx_children_ask {
    int64_t id, view, rev, have;    /* have: -1 for vkids */
    const unsigned char *node;      /* the node's id (empty: the root) */
    size_t node_len;
} rx_children_ask;
/* an action: key [a-z0-9_]{1,40}, icon a built-in name or NULL, where "title", "node" or "none" */
void rx_patch_action(rx_patch *p, const char *key, const char *icon, const char *where, const char *title);
void rx_patch_select_mode(rx_patch *p, int multi);
void rx_x_inline(rx_buf *x, const char *keys);     /* space separated, 4 at most */
void rx_x_menu(rx_buf *x, const char *keys);       /* 16 at most */
/* expands a node's ancestors and scrolls it into view; how "-" or a comma list of select, focus, expand */
int rx_vreveal(rx_ext *ext, int64_t view, const char *id, size_t len, const char *how);
void rx_patch_title(rx_patch *p, const char *title);         /* at most 80 bytes; "" the manifest's */
void rx_patch_badge(rx_patch *p, int n);
void rx_patch_message(rx_patch *p, const char *text);        /* "" clears it */
/* state: "ready", "loading", "empty", "error"; text and keys (space separated, up to 3) for the last two */
void rx_patch_state(rx_patch *p, const char *state, const char *text, const char *keys);
void rx_patch_busy(rx_patch *p, int on);
/* records: n node records (rx_node_rec); parent "" with plen 0 is the root; more 1: "Load more" */
void rx_patch_kids(rx_patch *p, const char *parent, size_t plen, const rx_buf *records, int n, int more);
void rx_patch_add(rx_patch *p, const char *parent, size_t plen, const rx_buf *records, int n, int more);
void rx_patch_set(rx_patch *p, const rx_buf *record);
void rx_patch_del(rx_patch *p, const char *id, size_t len);
void rx_patch_expand(rx_patch *p, const char *id, size_t len);
void rx_patch_collapse(rx_patch *p, const char *id, size_t len);
void rx_patch_raw(rx_patch *p, const char *head, const void *data, size_t len);
void rx_patch_free(rx_patch *p);
/* one node record: id (1 to 1000 bytes, no LF or NUL), a one-line label, kids "leaf", "closed" or
 * "open", its extra records (or NULL); 0, or -1 for a bad id */
int rx_node_rec(rx_buf *out, const char *id, size_t idlen, const char *label, const char *kids, const rx_buf *extra);
void rx_x_desc(rx_buf *x, const char *text);
void rx_x_tip(rx_buf *x, const char *text);
/* spec: a built-in icon name, "file:NAME" or "ext:KEY"; color a color name or "-" */
void rx_x_icon(rx_buf *x, const char *spec, const char *color);
void rx_x_color(rx_buf *x, const char *color);
void rx_x_hl(rx_buf *x, int from, int to);
void rx_x_badge(rx_buf *x, int n);
void rx_x_check(rx_buf *x, int on);
void rx_x_ctx(rx_buf *x, const char *value);
void rx_x_data(rx_buf *x, const void *data, size_t len);
void rx_x_open(rx_buf *x, const char *path, int line, int col);
void rx_x_command(rx_buf *x, const char *name, const void *args, size_t len);
/* any record: "HEAD LEN" LF and the data */
void rx_rec(rx_buf *b, const char *head, const void *data, size_t len);
/* the view's new revision, or -1 (rx_error says why: noview, syntax op N, badpos OP ID, toobig) */
int64_t rx_view_patch(rx_ext *ext, int64_t view, const rx_patch *p);
/* opens a declared view's place on it; focus 1 also gives it the keyboard; its view number */
int64_t rx_vshow(rx_ext *ext, const char *name, int focus);
/* Log views (feature "view-log", kind = log): n lines appended, sevs[i] "trace", "debug", "info",
 * "warn", "error" or "-", texts[i] one line each (SGR colors kept); clear removes them all. */
void rx_patch_log(rx_patch *p, int n, const char *const *sevs, const char *const *texts);
void rx_patch_clear(rx_patch *p);
/* a retained view (kind "tree"; "log", "form", "table" or "doc" with view-log, view-form, view-table
 * or view-doc) in an editor tab of its own, shown at once;
 * its view number, or -1. rx_unview closes it. */
int64_t rx_vtab(rx_ext *ext, const char *kind, const char *title);
/* Images (feature "view-images"): a PNG, JPEG or GIF decoded once; its handle, or -1 */
int64_t rx_image(rx_ext *ext, const void *data, size_t len);
int rx_unimage(rx_ext *ext, int64_t image);
/* Local port addition: ordered best-effort cleanup without waiting a frame for
 * its acknowledgment. The next synchronous request consumes the reply. */
int rx_unimage_async(rx_ext *ext, int64_t image);
/* the picture shape that draws an image scaled into the box */
void rx_pic_image(rx_buf *picture, double x, double y, double w, double h, int64_t image);

/* Tables (feature "view-table", kind = table): columns (1-16) and the arrow of a column the
 * extension sorted (col from 1, 0 none); rx_x_cell adds a node's next column (2, 3, ...). A sort
 * event (kind "sort", argv REV COL asc|desc) comes when the user sorts while rows are paged. */
typedef struct rx_column { const char *title; int right; int min; int sort; } rx_column;
void rx_patch_columns(rx_patch *p, int n, const rx_column *cols);
void rx_patch_sorted(rx_patch *p, int col, int desc);
void rx_x_cell(rx_buf *x, const char *text);
/* Forms (feature "view-form", kind = form): field records built with rx_field_rec (type text,
 * area, number, check, select, button, label; extra records rx_fx_* or rx_rec), then the whole
 * form or one field. Events: change (argv REV KEY SEQ LEN, body the value), press, submit. */
void rx_field_rec(rx_buf *out, const char *key, const char *type, int row, int weight, const char *label,
                  const rx_buf *extra);
void rx_fx_value(rx_buf *x, int64_t seq, const char *value);     /* seq -1: "-" */
void rx_fx_placeholder(rx_buf *x, const char *text);
void rx_fx_options(rx_buf *x, const char *lines);                /* one option a line */
void rx_fx_msg(rx_buf *x, const char *severity, const char *text);
void rx_patch_form(rx_patch *p, const rx_buf *records, int n);
void rx_patch_field(rx_patch *p, const rx_buf *record);
/* Doc views (feature "view-doc", kind = doc): Markdown (at most 1 MB), the folder its relative
 * links name, the document the view follows (0 stops). A link event (kind "link", body the
 * target) comes for links rhun does not follow. */
void rx_patch_doc(rx_patch *p, const char *markdown, size_t len);
void rx_patch_base(rx_patch *p, const char *folder);
void rx_patch_follow(rx_patch *p, int64_t doc);
/* Quick input (feature "quick-input"): a step body built with rx_qs_* (only the records given
 * change on rx_quick_set); items with rx_qi_rec (sep 1: a separator) and rx_qs_items. rx_quick
 * returns Q or -1 (busy while one, the palette or a prompt shows); events reach quick_event. */
void rx_qs_title(rx_buf *b, const char *title);
void rx_qs_mode(rx_buf *b, const char *mode);                   /* "pick" or "input" */
void rx_qs_step(rx_buf *b, int n, int of);
void rx_qs_placeholder(rx_buf *b, const char *text);
void rx_qs_prompt(rx_buf *b, const char *text);
void rx_qs_value(rx_buf *b, int64_t seq, const char *text);     /* seq -1: "-" */
void rx_qs_flags(rx_buf *b, const char *flags);                 /* space separated, "" clears */
void rx_qs_msg(rx_buf *b, int64_t seq, const char *severity, const char *text);
void rx_qs_active(rx_buf *b, const char *id);
void rx_qi_rec(rx_buf *out, const char *id, const char *label, int sep, const rx_buf *extra);
void rx_qs_items(rx_buf *b, const rx_buf *records, int n);
int64_t rx_quick(rx_ext *ext, const rx_buf *step);
int rx_quick_set(rx_ext *ext, int64_t q, const rx_buf *step);
int rx_quick_close(rx_ext *ext, int64_t q);

/* Notifications (protocol 1.1, feature "notify"): a card built with rx_note_*, shown with
 * rx_notify(ext, "info"|"warning"|"error", &note); rx_note_free after. */
typedef struct rx_note_s { rx_buf msg, extra; int buttons; } rx_note;
void rx_note_init(rx_note *n, const char *msg);                /* at most 2000 bytes; LF breaks a line */
void rx_note_detail(rx_note *n, const char *text);             /* a muted line under it */
void rx_note_sticky(rx_note *n);                               /* it stays until closed */
/* a button (3 at most): command NULL reports note_event pressed N, else that command runs with args */
void rx_note_button(rx_note *n, const char *title, const char *command, const void *args, size_t len);
void rx_note_free(rx_note *n);
int64_t rx_notify(rx_ext *ext, const char *severity, const rx_note *n);   /* its NOTE, or -1 */
int rx_notify_set(rx_ext *ext, int64_t note, const rx_note *n);
int rx_unnotify(rx_ext *ext, int64_t note);
/* Progress (feature "progress"): where "status", "note" or "view:VIEW"; cancel 1 offers Cancel.
 * Nothing shows when it ends within 500 ms. Its P, or -1. */
int64_t rx_progress(rx_ext *ext, const char *where, int cancel, const char *title);
int rx_progress_set(rx_ext *ext, int64_t progress, int percent, const char *msg);   /* -1: indeterminate */
int rx_progress_end(rx_ext *ext, int64_t progress);

/* Feature agents: replace its rows in the agents panel; say a session has total messages, the first
 * changed one at first. 0, or -1 with rx_error. */
int rx_agent_sessions(rx_ext *ext, const rx_session *s, int n);
int rx_agent_changed(rx_ext *ext, const char *sid, int64_t total, int64_t first);
/* a transcript answer: messages (role user, assistant, tool, result, error or info), then finish;
 * set t->total to the session's count first. Stops adding at 2000 messages or 4 MB. */
void rx_transcript_add(rx_transcript *t, const char *role, const char *name, const char *text, size_t len,
                       const char *model);
void rx_transcript_finish(rx_transcript *t, rx_buf *out);
/* Feature edit-review: the proposal's id, or -1; unpropose withdraws one. The edit goes in 12g's
 * grammar whatever was agreed (the Review tab reads 12g): a hash as "-", without its label and
 * confirm; file operations fail it as unknown. rx_proposal_body is the body it sends: 0, or -1 with
 * edit->fail and edit->why saying why. */
int64_t rx_propose(rx_ext *ext, const char *label, const char *sid, rx_wedit *edit);
int rx_proposal_body(const char *label, const char *sid, rx_wedit *edit, rx_buf *out);
int rx_unpropose(rx_ext *ext, int64_t proposal);
int rx_parse_proposal(const rx_event *ev, rx_proposal_event *out);  /* 1 for a proposal event */
/* Feature tools: the list (rx_tools_free it), a call's output (the user may deny: refused). */
int rx_tools(rx_ext *ext, rx_tool **tools, int *n);
int rx_tools_parse(const unsigned char *body, size_t len, rx_tool **tools, int *n);
void rx_tools_free(rx_tool *tools, int n);
int rx_tool_call(rx_ext *ext, const char *tool, const void *input, size_t len, rx_buf *out);
/* Feature agent-prompts: a card in a session, a prompt line and the options as buttons. Returns
 * the index into options of the one the user picked, from 0 (the wire's answer counts from 1, so
 * this is that number minus 1), or -1 (cancelled, or another error: rx_error says which). */
int rx_agent_ask(rx_ext *ext, const char *sid, const char *prompt, const char *const *options, int n);

/* The features of protocol 1.1's editor model, for rx_run and rx_has. */
#define RX_FEATURE_WORKSPACE_EDITS "workspace-edits"
#define RX_FEATURE_FILE_OPERATIONS "file-operations"
#define RX_FEATURE_SELECTIONS "selections"
#define RX_FEATURE_REVEAL "reveal"
#define RX_FEATURE_DIFF "diff"
#define RX_FEATURE_LINKED_EDITING "linked-editing"

/* Speaks the protocol on standard input and output until rhun says bye (0) or goes away (1). */
int rx_run(const char *features, const rx_handlers *h, void *user);
/* Whether rhun agreed to a feature. */
int rx_has(rx_ext *ext, const char *feature);

/* Requests. They block until rhun answers: 0 (or an id, a version) on ok, -1 on an error reply,
 * whose "code text" rx_error then says (code: syntax unknown needs nodoc nofile readonly stale
 * badpos toobig busy cancelled refused noview), for the log rather than for the user. */
const char *rx_error(rx_ext *ext);
int rx_toast(rx_ext *ext, const char *text);
int rx_status(rx_ext *ext, const char *text);   /* "" clears it */
/* the document's text, malloc'd and NUL-terminated (free() it), or NULL */
unsigned char *rx_text(rx_ext *ext, rx_doc doc, size_t *len, int64_t *version);
int64_t rx_insert(rx_ext *ext, rx_doc doc, int64_t version, const char *text, size_t len);
int64_t rx_replace(rx_ext *ext, rx_doc doc, int64_t version, rx_range r, const char *text, size_t len);
int rx_select(rx_ext *ext, rx_doc doc, rx_range r);
rx_doc rx_open(rx_ext *ext, const char *path, rx_pos at);
int rx_register(rx_ext *ext, const char *name, const char *keys, const char *title);
/* Protocol 1.1, negotiated "commands": remove an own runtime command and its shortcut. */
int rx_unregister(rx_ext *ext, const char *name);
/* Declarations after rx_unregister. Coalesced invalidations always need a rescan. */
int64_t rx_watch_workspace(rx_ext *ext);
int rx_unwatch_workspace(rx_ext *ext, int64_t watch);
int rx_parse_workspace_watch(const rx_event *event, int64_t *watch, int *rescan);
/* Resources group (protocol 1.1). Feature "idle": not stopped for idleness while busy. */
int rx_busy(rx_ext *ext, int busy);
/* Feature "secrets" ([needs] secrets = yes): KEY is 1 to 64 bytes of a-z 0-9 _ . -, a value at
 * most 8192 bytes. rx_secret_get: 1 with the value in *value (malloc'd, free() it) and its bytes
 * in *len, 0 when nothing is stored, -1 on an error. rx_secret_keys: the keys, each followed by LF
 * (malloc'd, NUL-terminated, free() it), or NULL on an error. */
int rx_secret_get(rx_ext *ext, const char *key, unsigned char **value, size_t *len);
int rx_secret_set(rx_ext *ext, const char *key, const void *value, size_t len);
int rx_secret_delete(rx_ext *ext, const char *key);
char *rx_secret_keys(rx_ext *ext, size_t *len);
int rx_secret_key_valid(const char *key);
/* Feature uris: SCHEME:REST, SCHEME [a-z][a-z0-9+.-] of 2 to 32 bytes, REST 1 to 4095 bytes
 * without LF, CR or NUL. rx_open takes a URI too; rx_content_changed asks rhun to read again. */
int rx_uri_valid(const char *uri);
int rx_content_changed(rx_ext *ext, const char *uri);
/* Feature urls. rx_open_url asks rhun to open an http(s) URL in the user's browser (the user sees
 * the extension and the host, and chooses): 1 opened, 2 copied (the user copied the link), -1 on
 * an error ("cancelled" when the user said no). rx_external_uri: the URI the user's browser can use
 * for a loopback URL or a rhun: link; rx_link: a one-shot rhun://ext/OWNER/NAME/TOKEN link (ten
 * minutes) to append a path and query to; both malloc'd and NUL-terminated (free() them), or NULL.
 * rx_parse_link reads the link event (also given to the link handler). */
int rx_open_url(rx_ext *ext, const char *url);
char *rx_external_uri(rx_ext *ext, const char *uri);
char *rx_link(rx_ext *ext);
int rx_parse_link(const rx_event *event, int64_t *id, const char **link, size_t *len);
/* Feature files ([needs] project = read): rx_find_files lists the workspace's files by glob,
 * rx_search_text searches their text (a literal, or with regex 1 an RNRX program), through rhun.
 * The batch callback gets each batch as it comes (its strings live until it returns) and returns
 * 1 to go on or 0 to stop: the run is then cancelled, the call returns -1 and rx_error says
 * "cancelled". On success out (if not NULL) has the reply: count paths or matches, files with
 * matches, skipped files, status complete, limit or partial. Folder 1 is the primary folder, 0
 * every folder; max is 1 to 100000 paths or 1 to 20000 matches; globs are relative to the folder
 * (* ** ? [..] {a,b}; without / they match the base name). */
/* Feature scoped-settings: the value of one of its own settings in force for a folder (its id, 0
 * for the window) and a language (a grammar name, NULL or "" for none): malloc'd, NUL-terminated
 * (free() it), or NULL on an error. The event "0 setting-changed KEY" says a stored value changed. */
char *rx_setting_get(rx_ext *ext, const char *key, int64_t folder, const char *lang);
/* Feature workspace-folders: rx_parse_folders reads "0 folders LEN" (the whole list, before ready
 * and again when it changes) into out (at most max; strings point into the event's body): the
 * number of folders, or -1. rx_folder_of: the id of the folder path is in, 0 for none, -1 on an
 * error. flags: 1 the primary folder, 2 trusted. */
typedef struct {
    int64_t id;
    int flags;
    char location[16];
    const char *name, *path;
    size_t name_len, path_len;
} rx_folder;
int rx_parse_folders(const rx_event *event, rx_folder *out, int max);
int64_t rx_folder_of(rx_ext *ext, const char *path);
#define RX_FILES_IGNORED 1       /* ignored files too: no .gitignore, no exclude setting */
#define RX_FILES_SKIP_HIDDEN 2
#define RX_FILES_FOLLOW_LINKS 4
#define RX_FILES_FOLDERS 8       /* find: folders too, with a trailing / */
#define RX_SEARCH_IGNORE_CASE 16
#define RX_SEARCH_WHOLE_WORD 32
typedef struct {
    int64_t folder;
    int max, flags;
    const char *const *include;
    int ninclude;
    const char *const *exclude;
    int nexclude;
} rx_file_query;
typedef struct { int64_t folder; const char *path; size_t path_len; } rx_found_path;
typedef struct { int line, col, end, pre; const char *text; size_t text_len; } rx_search_match;
typedef struct {
    int64_t folder;
    rx_doc doc;                 /* the open document searched, or 0 for the file on disk */
    int64_t version;
    const char *path;
    size_t path_len;
    int nmatches;
    const rx_search_match *matches;
} rx_found_file;
typedef struct { int64_t count, files, skipped; char status[16]; } rx_finished;
int rx_find_files(rx_ext *ext, const rx_file_query *q, int (*batch)(const rx_found_path *paths, int n, void *user),
                  void *user, rx_finished *out);
int rx_search_text(rx_ext *ext, const void *pattern, size_t len, int regex, const rx_file_query *q,
                   int (*batch)(const rx_found_file *files, int n, void *user), void *user, rx_finished *out);
#ifdef RHUNEXT_AUTH
/* Signing in (define RHUNEXT_AUTH before including; on Windows link ws2_32 and bcrypt; with
 * -std=c99 on Linux define _POSIX_C_SOURCE 200809L first): OAuth's authorization code flow with
 * PKCE (RFC 7636, S256) and the redirect received on a one-shot loopback HTTP listener on
 * 127.0.0.1. Pass rx_loopback_redirect_uri through rx_external_uri, open the authorization URL with
 * rx_open_url, then rx_loopback_wait: 1 with the redirect's query (malloc'd, free() it) once its
 * state matches, 0 on the timeout, -1 on an error parameter or a wrong state. The browser is told
 * "Signed in. You can close this tab." rx_query_get: a query value decoded (malloc'd) or NULL. */
typedef struct { char verifier[44], challenge[44]; } rx_pkce;
int rx_random_hex(char *out, size_t nbytes);            /* out holds 2 * nbytes + 1 */
int rx_pkce_new(rx_pkce *p);
void rx_pkce_challenge(const char *verifier, char out[44]);
void rx_sha256(const void *data, size_t len, unsigned char out[32]);
typedef struct { intptr_t fd; int port; } rx_loopback;
int rx_loopback_listen(rx_loopback *lb);
void rx_loopback_redirect_uri(const rx_loopback *lb, char *out, size_t cap);
int rx_loopback_wait(rx_loopback *lb, const char *state, int timeout_ms, char **query);
void rx_loopback_close(rx_loopback *lb);
char *rx_query_get(const char *query, const char *name);
#endif
/* Source control (protocol 1.1). Feature "git-data" with [needs] git = read: rhun's Git data.
 * Requests return 0 on ok (-1 and rx_error otherwise); results own their body: free them with
 * their _free function. Strings point into the body and are not NUL-terminated (use the lengths).
 * Feature "commit-message": rhun asks the commit_message handler for a message (below). */
typedef struct {
    rx_buf body;
    const char *root, *branch, *upstream;   /* work tree path; branch (7 hex digits when detached); "origin/main" or "" */
    size_t root_len, branch_len, upstream_len;
    char head[12];                          /* branch, detached, unborn */
    char state[12];                         /* clean, dirty, merging, rebasing */
    int64_t ahead, behind;
} rx_git_repo_info;
typedef struct { char x, y; const char *path; size_t path_len; } rx_git_status_entry;   /* '.' an unchanged side */
typedef struct { rx_buf body; rx_git_status_entry *entries; size_t n; } rx_git_status_list;
typedef struct {
    char sha[72];
    int64_t time;                           /* author time, seconds since 1970 */
    const char *author, *mail, *summary;
    size_t author_len, mail_len, summary_len;
} rx_blame_commit;
typedef struct { char sha[72]; int64_t l1, l2, orig; } rx_blame_run;   /* lines l1..l2 from commit sha (all 0s: not committed) */
typedef struct { rx_buf body; rx_blame_commit *commits; size_t ncommits; rx_blame_run *runs; size_t nruns; } rx_blame;
typedef struct {
    char sha[72];
    const char *parents;                    /* SHAs joined by ',', "" for a root commit */
    size_t parents_len;
    int64_t time;
    const char *author, *subject;
    size_t author_len, subject_len;
} rx_log_entry;
typedef struct { rx_buf body; rx_log_entry *entries; size_t n; } rx_log;
int rx_git_repo(rx_ext *ext, rx_git_repo_info *out);
int rx_git_status(rx_ext *ext, rx_git_status_list *out);
int rx_git_blame(rx_ext *ext, rx_doc doc, rx_blame *out);
/* path NULL: the history of HEAD; l1 = l2 = 0: the file's commits; else its lines l1..l2 at HEAD */
int rx_git_log(rx_ext *ext, const char *path, int l1, int l2, int max, rx_log *out);
int rx_git_show(rx_ext *ext, const char *rev, const char *path, rx_buf *out);     /* the file's bytes at rev */
int rx_git_diff_open(rx_ext *ext, const char *rev1, const char *rev2, const char *path);  /* rev2 NULL: the work tree */
/* decoders of the bodies (out->body already filled): 0, or -1 for a malformed body */
int rx_parse_git_repo(rx_git_repo_info *out);
int rx_parse_git_status(rx_git_status_list *out);
int rx_parse_blame(rx_blame *out);
int rx_parse_log(rx_log *out);
void rx_git_repo_free(rx_git_repo_info *r);
void rx_git_status_free(rx_git_status_list *s);
void rx_blame_free(rx_blame *b);
void rx_log_free(rx_log *l);
int rx_is_git_changed(const rx_event *event);       /* "0 git changed": ask again */
int rx_valid_rev(const char *rev);
/* rhun's checks of a commit message: trims spaces, tabs and line breaks (start and length in
 * *start, *len); 0 when it is 1 to 8192 bytes without control characters but tab, CR, LF */
int rx_check_commit_message(const char *m, size_t n, size_t *start, size_t *len);
/* what commit_message gets: a cancel event names id */
typedef struct rx_commit_message_ask_s { int64_t id; const char *branch, *diff; size_t branch_len, diff_len; } rx_commit_message_ask;
int rx_parse_commit_message(int64_t id, const unsigned char *body, size_t len, rx_commit_message_ask *out);
/* what quickdiff gets; rx_quickdiff_refresh asks rhun to ask again for a file (NULL: every file) */
typedef struct rx_quickdiff_ask_s { int64_t id; rx_doc doc; int64_t version; } rx_quickdiff_ask;
int rx_quickdiff_refresh(rx_ext *ext, const char *path);

/* Feature "debug" ([debugger.TYPE] sections, [needs] debug = yes). A configuration is a flat list of
 * typed leaves with dotted paths ("args.0", "env.LOG"): kind 's' string, 'n' integer (|v| < 2^53),
 * 'b' "true"/"false", 'z' null (empty value). Entries own their path and value. */
typedef struct { char kind; char *path; char *value; size_t value_len; } rx_debug_entry;
typedef struct rx_debug_config_s { rx_debug_entry *entries; size_t n, cap; } rx_debug_config;
/* the context record of a configuration body rhun sends: the active document (0: none) */
typedef struct { rx_doc doc; rx_pos at; rx_buf path; } rx_debug_context;
/* replaces the leaf (and anything under its path) or adds it; -1 for a bad kind, path or value */
int rx_debug_config_set(rx_debug_config *c, char kind, const char *path, const char *value);
void rx_debug_config_remove(rx_debug_config *c, const char *path);
const rx_debug_entry *rx_debug_config_get(const rx_debug_config *c, const char *path);
void rx_debug_config_free(rx_debug_config *c);
int rx_debug_config_encode(const rx_debug_config *c, rx_buf *out);
/* reads a configuration body and its extra records (ctx may be NULL); 0, or -1 when it does not parse */
int rx_debug_config_parse(const unsigned char *body, size_t len, rx_debug_config *out, rx_debug_context *ctx);
/* starts a session of one of the extension's own debuggers (the config's "type"), within 10 s of
 * one of its commands; the session's id, or -1 (rx_error: refused, cancelled, busy, toobig ...) */
int64_t rx_debug_start(rx_ext *ext, const char *name, const rx_debug_config *c);
/* stops a session this extension started (as the toolbar's Stop); 0 once the stop was asked (its
 * debug-ended follows), -1 (rx_error: refused, not a session it started) */
int rx_debug_stop(rx_ext *ext, int64_t session);
/* "0 debug-ended SESSION CODE": 1 with the session and code (*has_code 0 when unknown), else 0 */
int rx_parse_debug_ended(const rx_event *ev, int64_t *session, int *code, int *has_code);
/* what debug_resolve gets */
typedef struct rx_debug_resolve_ask_s {
    int64_t id;
    const char *type, *request;     /* request: launch or attach */
    int no_debug;
    rx_debug_config config;
    rx_debug_context ctx;
} rx_debug_resolve_ask;
/* Feature "scm": version control panels. rx_scm_register adds a sidebar section for the
 * repository at root (absolute) and returns its handle (-1 on an error); rx_scm_update replaces
 * what it shows with a body built by the appenders below, in the body's order: rx_scm_head, its
 * nactions buttons (rx_scm_action), then per group rx_scm_group, the group's actions, and its
 * resources, each rx_scm_res followed by nrec records (rx_scm_click first, rx_scm_action,
 * rx_scm_tip). Icons are tree icon names ("check", "refresh", NULL for none); colors added,
 * modified, deleted, conflict, untracked, ignored or NULL. Texts are at most 200 bytes without
 * CR or LF (a tip 1000); rhun checks the rest and answers syntax. */
int64_t rx_scm_register(rx_ext *ext, const char *root, const char *label);
int rx_scm_update(rx_ext *ext, int64_t scm, const rx_buf *body);
int rx_scm_input(rx_ext *ext, int64_t scm, rx_buf *out);           /* the message box's text */
int rx_scm_input_set(rx_ext *ext, int64_t scm, const char *text);  /* "" clears it */
void rx_scm_head(rx_buf *b, const char *status, const char *placeholder, int nactions, int ngroups);
void rx_scm_action(rx_buf *b, const char *icon, const char *command, const char *args, size_t args_len, const char *label);
void rx_scm_group(rx_buf *b, const char *id, const char *label, int nactions, int nres);
void rx_scm_res(rx_buf *b, char code, const char *color, const char *path, int nrec);
void rx_scm_click(rx_buf *b, const char *command, const char *args, size_t args_len);
void rx_scm_tip(rx_buf *b, const char *tip);

/* Commands (protocol 1.1, feature "command-args"). An invocation is "0 run EXT.NAME DOC FROM LEN"
 * and its body: from is palette (no body), key (the binding's ARGS), menu (TARGET LF the item's
 * args) or ref (a command reference's ARGS); "" and no body without the feature. */
typedef struct rx_invocation_s {
    const char *command;            /* EXT.NAME */
    const char *name;               /* NAME */
    rx_doc doc;                     /* the document it is about, or 0 */
    const char *from;
    const unsigned char *body;      /* body[body_len] is 0 */
    size_t body_len;
} rx_invocation;
/* a menu invocation's TARGET line ("file PATH", "tab doc DOC PATH", "item VIEW ID" …) and the
 * item's args: 0, or -1 for other invocations */
int rx_menu_target(const rx_invocation *inv, const char **target, size_t *target_len,
                   const unsigned char **args, size_t *args_len);
/* what call gets: CALLER is OWNER/NAME of the extension that called; strings live until it returns */
typedef struct rx_call_ask_s {
    int64_t id;
    const char *command, *name, *caller;
    rx_doc doc;                     /* the active document, or 0 */
    const unsigned char *args;
    size_t args_len;
} rx_call_ask;
/* append the record "command NAME LEN" LF ARGS (a reference to one of its own commands, local
 * NAME, at most 8192 bytes of ARGS) to out: 0, or -1 without changing out */
int rx_command_ref(rx_buf *out, const char *name, const void *args, size_t len);
/* run a rhun built-in ("goto_line", "rhun.find") or another extension's callable command
 * ("OWNER/EXT.NAME") with ARGS (at most 65536 bytes); its result is appended to result (may be
 * NULL; always empty for a built-in) */
int rx_run_command(rx_ext *ext, const char *name, const void *args, size_t len, rx_buf *result);
/* Feature "context-keys": set one of its keys for when clauses (OWNER/NAME.KEY), one line of at
 * most 256 bytes; rx_remove_context brings back the [context] default */
int rx_set_context(rx_ext *ext, const char *key, const char *value);
int rx_remove_context(rx_ext *ext, const char *key);
/* 1 when the bytes show as they are on one line: UTF-8 without control characters (C0, DEL, C1),
 * bidirectional controls or zero-width characters */
int rx_plain_text(const char *s, size_t n);
/* When clauses (protocol 1.1, "When clauses"): 0 when the clause compiles as rhun compiles it,
 * else -1 with the byte offset rhun reports in *offset and its reason in *msg (static). */
int rx_when_check(const char *clause, size_t len, size_t *offset, const char **msg);

/* Feature "status-items": a status bar item, at most 8 per extension (manifest items included).
 * align "left" or "right" (NULL: right); priority -1000..1000; text one line of at most 120 bytes
 * ("$(NAME)" an icon); tooltip at most 500 bytes; command one of its own (local name, NULL for
 * none) with args; color fg muted accent error warning success (NULL: muted); background NULL,
 * error or warning; when a clause or NULL. */
typedef struct {
    const char *name, *align;
    int priority;
    const char *text, *tooltip, *command;
    const void *args;
    size_t args_len;
    const char *color, *background, *when;
} rx_status_item;
int rx_status_item_encode(const rx_status_item *item, rx_buf *out);   /* the body; -1 when invalid */
int rx_set_status_item(rx_ext *ext, const rx_status_item *item);
int rx_remove_status_item(rx_ext *ext, const char *name);


int rx_cmd(rx_ext *ext, const char *name);
int64_t rx_view(rx_ext *ext, const char *title);        /* feature views: a tab; its id */
int rx_draw(rx_ext *ext, int64_t view, const rx_buf *picture);
int rx_unview(rx_ext *ext, int64_t view);
/* a workspace edit (12g; documents write) across documents and files: checked whole; on an error
 * nothing changes. Without "workspace-edits" each document changes as one undo step and files
 * that are not open open in tabs, unsaved; with it the edit is one transaction (files that are not
 * open are edited on disk, it undoes as one step, rx_apply_error says which entry failed), and with
 * "file-operations" it creates, renames and deletes files. The edit is used up; one that cannot be
 * sent (past the limits, or using what was not agreed) fails without being sent. For a language
 * server's workspace/applyEdit. */
int rx_apply(rx_ext *ext, rx_wedit *edit);
/* Negotiated save-actions: an intermediate edit for this active save invocation. The edit is
 * used up on every path. Returns the credited native version, or -1 on failure. */
int64_t rx_save_apply(rx_ext *ext, int64_t request_id, rx_doc doc, int64_t version, rx_wedit *edit);
/* Prepared UTF-8 text, with byte offsets and an earlier primary parent (one-based, 0 root).
 * First positive group occurrence is primary, repetitions are mirrors, group 0 the final selection (empty for a caret).
 * Text/fields are borrowed only for the duration of each call. */
typedef struct { int64_t group; size_t start, end, parent; } rx_snippet_field;
typedef struct {
    const char *text;
    size_t len;
    const rx_snippet_field *fields;
    size_t nfields;
} rx_snippet;
/* Option bytes may be empty or contain newlines and NUL. Storage is borrowed until encoding. */
typedef struct { const char *text; size_t len; } rx_snippet_option;
typedef struct { int64_t group; const rx_snippet_option *options; size_t noptions; } rx_snippet_choice;
/* Compiled program bytes are borrowed only during validation or encoding. */
typedef struct {
    const void *regex;
    size_t regex_len;
    const void *replacement;
    size_t replacement_len;
} rx_snippet_transform_program;
typedef struct { size_t field; rx_snippet_transform_program transform; } rx_snippet_field_transform;
typedef struct {
    const rx_snippet *snippet;
    const rx_snippet_choice *choices;
    size_t nchoices;
    /* Borrowed one-based nonprimary field indices with unequal initial text. */
    const size_t *initial_variants;
    size_t ninitial_variants;
    const rx_snippet_field_transform *transforms;
    size_t ntransforms;
} rx_snippet_program;
/* part: 0 arguments/combined size, 1 regex, 2 replacement. Outputs are optional.
 * error_offset is program-local, zero on success or an argument/size error. */
int rx_snippet_transform_program_validate(const rx_snippet_transform_program *program, int *part, size_t *error_offset);
/* Validate canonical compiled SNTX bindings and nested programs without allocation or
 * execution. Does not check field existence/ownership. error_offset is optional;
 * it receives zero on success, otherwise the absolute first invalid byte. */
int rx_snippet_transform_validate(const void *bytes, size_t len, size_t *error_offset);
int rx_snippet_program_validate(const rx_snippet_program *program);
int rx_snippet_program_encode(const rx_snippet_program *program, rx_buf *out);
int64_t rx_insert_snippet_program(rx_ext *ext, rx_doc doc, int64_t version, rx_range range, const rx_snippet_program *program);

/* Owned read-only insertion context. Free a successful result before reusing it.
 * Strings preserve their byte lengths, including NUL. Never includes the clipboard. */
typedef struct {
    int64_t version;
    rx_pos position;
    int tab_width, insert_spaces, crlf;
    rx_buf selected_text, current_line, path, workspace, indent, line_comment;
} rx_snippet_snapshot;
void rx_snippet_snapshot_free(rx_snippet_snapshot *snapshot);
int rx_get_snippet_snapshot(rx_ext *ext, rx_doc doc, int64_t version, rx_range range, rx_snippet_snapshot *out);
int rx_snippet_validate(const rx_snippet *snippet);
/* Append an explicit snippet record, or return -1 without changing out for invalid input. */
int rx_snippet_encode(const rx_snippet *snippet, rx_buf *out);
int64_t rx_insert_snippet(rx_ext *ext, rx_doc doc, int64_t version, rx_range range, const rx_snippet *snippet);
/* ---- protocol 1.1: workspace edits as transactions ("workspace-edits") and file operations
 * ("file-operations"). With the feature, rx_apply's edit is one transaction: files that are not
 * open are edited on disk (a group from rx_wedit_file_hashed names the bytes it was made against),
 * documents written when the edit touches disk anyway, one undo everywhere. The body lists the
 * creates, then the text groups, then the renames, then the deletes, each in the order added: an
 * error's entry counts in that order. rx_apply, rx_wedit_finish_for and the answers that carry an
 * edit (rename, code actions and their resolve) follow the features rhun agreed: without
 * workspace-edits a hashed group, a label or confirm make the edit fail as unknown, and so do file
 * operations without both features; nothing rhun would refuse is sent. Format and save-actions
 * answers and rx_save_apply keep 12g's grammar whatever was agreed (those fail as unknown there
 * too); rx_propose sends 12g's grammar, a hash as "-" and without the label and confirm.
 * rx_wedit_finish is the body for a rhun that agreed both. */

/* the 64-bit FNV-1a hash of len bytes (offset basis 0xcbf29ce484222325, prime 0x100000001b3) */
static inline uint64_t rx_fnv1a64(const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}
/* a group for a file that is not open, made against data, its len bytes as read from disk (CR
 * included): the file must still hash to them (PRE fnv1a64:HEX), else the edit is stale. Positions
 * refer to rhun's text of it (CR before LF dropped). Add its edits with rx_wedit_edit. */
void rx_wedit_file_hashed(rx_wedit *e, const char *path, const void *data, size_t len);
/* file operations; paths absolute, a create path ending in "/" makes a folder. flags: create and
 * rename take RX_OVERWRITE (replace an existing file) and RX_IGNORE_EXISTING (skip the entry when
 * the path exists; overwrite wins), delete RX_RECURSIVE (a folder and everything in it) and
 * RX_IGNORE_MISSING; another flag fails the edit as syntax */
#define RX_OVERWRITE 1
#define RX_IGNORE_EXISTING 2
#define RX_RECURSIVE 4
#define RX_IGNORE_MISSING 8
void rx_wedit_create(rx_wedit *e, const char *path, int flags);
void rx_wedit_rename(rx_wedit *e, const char *from, const char *to, int flags);
void rx_wedit_delete(rx_wedit *e, const char *path, int flags);
/* names the change in the confirmation list, toasts and Undo: one line, cut at 200 bytes */
void rx_wedit_label(rx_wedit *e, const char *label);
/* shows the confirmation list even when the extension may write */
void rx_wedit_confirm(rx_wedit *e);
/* the body of e in the grammar ext agreed, appended to out, as rx_apply sends it: 0, or -1 when it
 * cannot be sent (e->fail and e->why say why: unknown for what needs a feature rhun did not agree,
 * toobig, ...); e is used up either way */
int rx_wedit_finish_for(rx_ext *ext, rx_wedit *e, rx_buf *out);
/* the last rx_apply refused with workspace-edits agreed ("error ID CODE ENTRY TEXT"): code
 * (syntax needs nofile nodoc stale readonly badpos toobig refused cancelled busy io, or unknown
 * for a feature rhun did not agree), entry the 1-based entry at fault (0 for none, and always 0
 * without the feature) and text one sentence; empty code after a successful rx_apply */
typedef struct { char code[24]; int entry; char text[480]; } rx_apply_error;
const rx_apply_error *rx_last_apply_error(rx_ext *ext);

/* LSP's ordered changes (documentChanges) onto rhun's phases. A change is a text edit of a file
 * (RX_CHANGE_EDIT: nedits edits, ranges in rhun's byte columns against the file's text at this
 * point of the sequence, see rx_locate; version the open document's or RX_ON_DISK with hashed and
 * hash for the file on disk; only a file's first edit sets the precondition, a created file takes
 * "-"), or a create, rename (path to to) or delete with its flags. Texts are borrowed until the
 * call returns. An edit after a rename names the file by its name before it; repeated edits of
 * one file become one group; edits of a file a later delete removes are left out. What the phases
 * cannot express (an edit after its file was renamed away or deleted, two file operations on one
 * path, an operation inside a folder another renames or deletes, an edit of a later rename's
 * target, a create after an edit of its path, an edit of text an earlier edit put in) returns -1
 * with the 0-based index of the change at fault in *bad and why in why (one line). On 0 the edit
 * is added to out (start from a zeroed rx_wedit) and, when origins is not NULL, origins[k] is the
 * change entry k + 1 came from (room for n). */
#define RX_CHANGE_EDIT 0
#define RX_CHANGE_CREATE 1
#define RX_CHANGE_RENAME 2
#define RX_CHANGE_DELETE 3
typedef struct {
    int kind;
    const char *path, *to;
    int64_t version;
    int hashed;
    uint64_t hash;
    const rx_range *ranges;
    const char *const *texts;
    const size_t *lens;             /* NULL: the texts are NUL-terminated */
    int nedits;
    int flags;
} rx_change;
int rx_wedit_from_ordered(rx_wedit *out, const rx_change *changes, int n, int *origins, int *bad, char *why, size_t cap);
/* where the text of path comes from after the n changes before: its name then in src (cap bytes)
 * and *created 1 when an earlier change created it (its text starts empty); -1 when an earlier
 * change renamed it away or deleted it */
int rx_locate(const rx_change *before, int n, const char *path, char *src, size_t cap, int *created);

/* ---- protocol 1.1: the editor model */

/* a selection: anchor and cursor (equal for a caret) */
typedef struct { rx_pos anchor, cursor; } rx_selection;
/* "selections": sets a document's selections, the first becoming primary (1 to 512) */
int rx_select_many(rx_ext *ext, rx_doc doc, const rx_selection *sels, int n);
/* the event "0 selections DOC N LEN" (every selection, the primary first): up to max into sels,
 * the count, or -1 when ev is not one or does not read */
int rx_parse_selections(const rx_event *ev, rx_doc *doc, rx_selection *sels, int max);
/* "reveal": scrolls the document's view so at shows, the cursor stays: where is "top", "center",
 * "nearest" or "center-if-outside" */
int rx_reveal(rx_ext *ext, rx_doc doc, rx_pos at, const char *where);
/* "reveal": lists locations (built with rx_location; used up) in the palette under title */
int rx_show_locations(rx_ext *ext, const char *title, rx_locations *locs);
/* "diff": a read-only tab with a unified diff from document left to right, titled title (one
 * line, 200 bytes), activated unless background; the tab's id (not a document), or -1 */
int64_t rx_diff(rx_ext *ext, rx_doc left, rx_doc right, const char *title, int background);
int rx_diff_close(rx_ext *ext, int64_t tab);
/* "diff" and "uris": rx_diff with sides named by URI (1 to 4128 bytes, no spaces or line breaks):
 * a virtual document's (opened without a tab when it is not open, closed with the last diff tab
 * showing it unless the user opened it meanwhile), a file: URI of an open file, or a document id */
int64_t rx_diff_uris(rx_ext *ext, const char *left, const char *right, const char *title, int background);
/* "reveal" and "uris": rx_reveal for the open document with that URI */
int rx_reveal_uri(rx_ext *ext, const char *uri, rx_pos at, const char *where);
/* the linked handler's answer: 2 to 64 ranges at the asked version with equal text, one holding
 * the cursor (another count is sent as none); word: the printable ASCII characters that may be
 * typed in them besides letters, digits and non-ASCII (rhun's default "-_.:" without it) */
void rx_linked_range(rx_linked *l, rx_range r);
void rx_linked_word(rx_linked *l, const char *chars);
void rx_linked_finish(rx_linked *l, rx_buf *out);

/* any request: "VERB ARGS" without the id (and with LEN last when there is a body); the reply's
 * fields after "ok ID" go to fields (NUL-terminated), a text/range/ask body to body */
int rx_request(rx_ext *ext, const char *verb_args, const void *body, size_t len, rx_buf *fields, rx_buf *body_out);

void rx_buf_add(rx_buf *b, const void *data, size_t len);
void rx_buf_str(rx_buf *b, const char *s);
void rx_buf_fmt(rx_buf *b, const char *fmt, ...);
void rx_buf_free(rx_buf *b);

/* Pictures (12a), appended to a buffer: logical pixels; colors are theme names (fg muted bg
 * panel border accent error warning success), chart series c1 to c8, or "#rrggbb[aa]". */
void rx_rect(rx_buf *p, double x, double y, double w, double h, const char *color);
void rx_rrect(rx_buf *p, double x, double y, double w, double h, double r, const char *color);
void rx_line(rx_buf *p, double x1, double y1, double x2, double y2, double width, const char *color);
void rx_poly(rx_buf *p, double width, const char *color, const double *xy, int points);
void rx_fill(rx_buf *p, const char *color, const double *xy, int points);
void rx_circle(rx_buf *p, double x, double y, double r, const char *color);
void rx_ring(rx_buf *p, double x, double y, double r, double width, const char *color);
void rx_pie(rx_buf *p, double x, double y, double r, double from, double to, const char *color);
/* size small normal large mono; align left center right; y is the line's top */
void rx_label(rx_buf *p, double x, double y, const char *size, const char *align, const char *color, const char *text);
void rx_clip(rx_buf *p, double x, double y, double w, double h);
void rx_unclip(rx_buf *p);

/* Completion lists (12b): start from a zeroed rx_list (rx_list l = {0}; in C++ rx_list l{};), add
 * items, then rx_list_finish writes the body; with no items it is the empty list. Set l.more = 1
 * when you cut the list short: typing in the word then asks again (it is set for you past 4096
 * items or 1 MB). Give every candidate for the word: rhun filters and orders by what is typed.
 * kind is text method function field variable class struct interface module property enum
 * keyword snippet constant value file folder. insert NULL is the label; detail and doc may be
 * NULL. */
typedef struct { rx_buf items; int n; int more; } rx_list;
/* Empty prepared text stays empty. Invalid field metadata falls back to plain prepared text.
 * Additional edits and documentation follow the ordinary completion API. */
void rx_list_item_snippet_program(rx_list *list, rx_range range, const char *kind, const char *label,
                         const rx_snippet_program *program, const char *detail, const char *doc,
                         const rx_range *edits, const char *const *texts, int nedits);
void rx_list_item_snippet(rx_list *list, rx_range range, const char *kind, const char *label,
                         const rx_snippet *snippet, const char *detail, const char *doc,
                         const rx_range *edits, const char *const *texts, int nedits);
void rx_list_item(rx_list *l, rx_range r, const char *kind, const char *label, const char *insert,
                  const char *detail, const char *doc);
/* the same with extra edits made with it (an import), not overlapping r or each other */
void rx_list_item_edits(rx_list *l, rx_range r, const char *kind, const char *label, const char *insert,
                        const char *detail, const char *doc, const rx_range *edits, const char *const *texts, int nedits);
/* the same with the cursor cursor bytes into insert when the item is chosen (a snippet's $0); -1,
 * or an offset past insert or inside a character, leaves it after insert */
void rx_list_item_cursor(rx_list *l, rx_range r, const char *kind, const char *label, const char *insert,
                         const char *detail, const char *doc, const rx_range *edits, const char *const *texts,
                         int nedits, long cursor);
void rx_list_finish(rx_list *l, rx_buf *out);
/* signature help (12c): the active parameter's bytes in the signature, from -1 for none */
void rx_signature_answer(rx_buf *out, const char *signature, int from, int to, const char *doc);

/* Protocol 1.1, complete-items: an item's other records, read by rhun with complete-items agreed.
 * Start from a zeroed one (rx_item_extra x = {0};); NULL and 0 leave a record out, as does one that
 * is not valid: resolve data that is empty or past 16 KiB, a replace range that does not start where
 * the item's range does. commit keeps printable ASCII characters, at most 16, each once. */
typedef struct {
    const void *resolve;            /* data rhun hands back in complete-resolve */
    size_t resolve_len;
    int resolve_edits;              /* the resolution may add edits: accepting waits for it */
    const rx_range *replace;        /* the range that replaces the rest of the word */
    const char *commit;             /* characters that accept the item when typed */
    const char *filter;             /* one line, matched and ranked instead of the label */
    const char *suffix;             /* one line drawn after the label in the muted color */
    int preselect, deprecated, adjust;  /* selected first; struck through; later lines indented */
    int after_signature, after_complete;    /* after accepting: signature help, a new list */
} rx_item_extra;
/* rx_list_item_cursor with those records (extra NULL for none) */
void rx_list_item_extra(rx_list *l, rx_range r, const char *kind, const char *label, const char *insert,
                        const char *detail, const char *doc, const rx_range *edits, const char *const *texts,
                        int nedits, long cursor, const rx_item_extra *extra);
/* the answer to complete-resolve, records only: detail (one line, replaces the item's) and doc NULL
 * for none; edits as an item's, in the document at the list's version, apart from its range and
 * other edits; then what rhun asks for after accepting */
void rx_resolve_answer(rx_buf *out, const char *detail, const char *doc, const rx_range *edits,
                       const char *const *texts, int nedits, int after_signature, int after_complete);
/* signature-list: every overload, label one line, from -1 for no active parameter, doc NULL for
 * none; active is zero-based (another value shows the first). At most 32; none appends nothing,
 * which hides the help; past 64 KB the documentation goes, then overloads, until it fits. */
typedef struct { const char *label; int from, to; const char *doc; } rx_signature_info;
void rx_signatures_answer(rx_buf *out, const rx_signature_info *sigs, int n, int active);

/* Protocol 1.1, query-stream: part of the answer to the stream's request, in the grammar of its
 * answer (the count line and that many records), sent at once; it waits for rhun's reply, which is
 * the flow control. 0, or -1 when rhun did not agree query-stream ("unknown", nothing sent), the
 * stream is cancelled (nothing sent: stop working), or rhun answered an error: cancelled or toobig
 * cancel the stream, and rhun keeps what it has. rx_error says which. */
int rx_stream_part(rx_stream *s, const void *body, size_t len);
/* the same with a builder's records, which it empties */
int rx_stream_locations(rx_stream *s, rx_locations *l);
int rx_stream_items(rx_stream *s, rx_list *l);
/* how far the work has come: percent 0 to 100, or -1 for none; text one line, cut at 200 bytes at a
 * character. Each report extends the request's time limit. */
int rx_stream_progress(rx_stream *s, int percent, const char *text);
/* whether rhun no longer needs the answer */
int rx_stream_cancelled(const rx_stream *s);
/* Protocol 1.1, provider-scope: the values rhun uses for a [provides] key until the process stops:
 * a space-separated subset of the manifest's own values, "*" the manifest's, "-" none. */
int rx_provides(rx_ext *ext, const char *key, const char *values);

/* Protocol 1.1, prepare-rename: the range F2 selects and the name the prompt starts with (one line,
 * cut at 200 bytes at a character). rx_refuse_answer makes out the reason a request is refused (one
 * line, cut at 1000 bytes) and returns -1, for a handler to return. */
void rx_prepare_rename_answer(rx_buf *out, rx_range r, const char *placeholder);
int rx_refuse_answer(rx_buf *out, const char *why);
/* Protocol 1.1, code-actions: an action's other records; start from a zeroed one
 * (rx_action_extra x = {0};). NULL and 0 leave a record out, as does resolve data that is empty or
 * past 16 KiB, or a command reference rhun would refuse. */
typedef struct {
    int preferred;                  /* the one Auto Fix applies at once */
    const char *disabled;           /* why it cannot apply now (one line, cut at 1000 bytes) */
    const void *resolve;            /* data rhun hands back in code-action-resolve when it is chosen */
    size_t resolve_len;
    const char *command;            /* one of the extension's commands, run after the edit, and */
    const void *args;               /* its args */
    size_t args_len;
    const char *fixes;              /* the ids of the diagnostics it fixes, space separated */
} rx_action_extra;
/* A code action: kind hierarchical (quickfix, refactor.extract, source.organizeImports; NULL is
 * quickfix; cut at 128 bytes); title one line (an empty one leaves the action out); edit as
 * rx_action's (used up); extra NULL for none. An action that does nothing (no edit, resolve data or
 * command, and not disabled), or whose kind is not one of l->kinds, is left out. Past 1000 actions or
 * 4 MB the rest are left out. */
void rx_code_action(rx_code_actions *l, const char *kind, const char *title, rx_wedit *edit,
                    const rx_action_extra *extra);
/* whether an action of kind matches the comma-separated kinds asked: it equals or descends from one
 * (source.organizeImports.go matches source.organizeImports); any kind for NULL, "" or "-" */
int rx_kind_wanted(const char *kinds, const char *kind);
/* The bodies, appended to out, for answers built outside a handler; each empties its builder. A
 * builder's caps is 0 there: the edits in the grammar of a rhun that agreed workspace-edits and
 * file-operations (in a handler rx_run sets it to what was agreed, and an action whose edit needs
 * more is left out). rx_resolved_finish returns -1, appending nothing, when the edit cannot be sent
 * (out->edit.fail and out->edit.why say why). */
void rx_code_actions_finish(rx_code_actions *l, rx_buf *out);
int rx_resolved_finish(rx_resolved *r, rx_buf *out);

/* Protocol 1.1, hover-query: the range the text describes (all 0 for the position only; one whose
 * end comes before its start is sent so) and the text (plain, or Markdown with markdown agreed), cut
 * at a character to fit 64 KiB. An empty text appends nothing: nothing to say. */
void rx_hover_answer(rx_buf *out, rx_range r, const char *text, size_t len);
/* selection-ranges: the next position's ranges, innermost first. Equal ranges are skipped, and the
 * first that runs backwards or does not contain the one before ends the group; at most 64. A group
 * left empty becomes the empty range at its position (it expands nothing), unless no later group has
 * ranges: the answer then ends before it. Groups past the positions are left out, and the outermost
 * ranges of the longest groups go until the answer fits 64 KiB. */
void rx_selection_group(rx_selections *s, const rx_range *ranges, int n);
/* hierarchy: an item (supertypes, subtypes; the roots: at most 64 and 256 KiB when l->roots), left
 * out as rx_hierarchy_item says or when its range runs backwards; a call: an item and its call sites
 * (1 to 1000: backward ones and those past 1000 are left out, a call without sites is left out).
 * Past 1000 records or 1 MiB the rest are left out. */
void rx_hierarchy_add(rx_hierarchy *l, const rx_hierarchy_item *it);
void rx_call_add(rx_calls *l, const rx_hierarchy_item *it, const rx_range *sites, int nsites);
/* workspace-symbols: a symbol anywhere in the project, the file by its absolute path or with path
 * NULL the open document doc; kind as rx_symbol's; name one line (an empty one leaves it out) and
 * detail (its container) NULL or one line, cut at 1000 bytes. Past 10000 or 1 MiB the rest are left
 * out. */
void rx_workspace_symbol(rx_workspace_symbols *l, const char *path, rx_doc doc, rx_range r, const char *kind,
                         const char *name, const char *detail);
/* document-links: ctrl+click on r opens target: an http or https URL, an absolute path optionally
 * followed by :LINE or :LINE:COL, or #DOC (rx_link_target_valid; another leaves the link out, as does
 * a range that runs backwards); tip one line or NULL, cut at 1000 bytes. Past 10000 or 1 MiB the
 * rest are left out. */
void rx_document_link(rx_links *l, rx_range r, const char *target, const char *tip);
int rx_link_target_valid(const char *target);
/* the bodies, appended to out, for answers built outside a handler; each empties its builder */
void rx_selections_finish(rx_selections *s, rx_buf *out);
void rx_hierarchy_finish(rx_hierarchy *l, rx_buf *out);
void rx_calls_finish(rx_calls *l, rx_buf *out);
void rx_workspace_symbols_finish(rx_workspace_symbols *l, rx_buf *out);
void rx_links_finish(rx_links *l, rx_buf *out);
/* query-stream parts of those answers: each sends its builder's records, which it empties */
int rx_stream_hierarchy(rx_stream *s, rx_hierarchy *l);
int rx_stream_calls(rx_stream *s, rx_calls *l);
int rx_stream_workspace_symbols(rx_stream *s, rx_workspace_symbols *l);

/* Protocol 1.1, file-diagnostics (needs documents = read): replaces this extension's diagnostics
 * for a file in rhun's Problems store: the file by its absolute path (version is not sent: pass
 * RX_ON_DISK), or with path NULL the open document doc at its current version. The file need not be
 * open: its diagnostics show when it opens. n 0 clears them. Diagnostics left out as rx_diagnostic
 * says, and those past 2000 or the 4 MiB body, are not sent. 0, or -1 with rx_error saying why:
 * stale (with the document's current version), needs, nodoc, nofile (an untitled document), syntax,
 * toobig; a target rhun would refuse fails as syntax without being sent. */
int rx_file_diagnostics(rx_ext *ext, const char *path, rx_doc doc, int64_t version, const rx_diagnostic *diags,
                        int n);
/* removes every set of diagnostics this extension gave */
int rx_reset_diagnostics(rx_ext *ext);
/* the records alone, appended to out; count gets how many */
void rx_file_diagnostics_records(const rx_diagnostic *diags, int n, rx_buf *out, int *count);

/* Definition and references (12d): a location is a file by its absolute path, or with path NULL
 * the open document doc (an untitled one), and a range there (in the open document when the file
 * is open, else in the file on disk); preview is its line's text, trimmed (NULL for none). One
 * location opens at once, several make a list. Past 10000 or 1 MB the rest are left out.
 * rhun drops a whole answer for one field past its cap (12d), so the builders cut names, titles,
 * details and previews to 1000 bytes and kind words to 40, at a character; a symbol or action whose
 * name or title is then empty, and a location whose path is over 4096 bytes, are left out. */
void rx_location(rx_locations *l, const char *path, rx_doc doc, rx_range r, const char *preview);
/* Symbols (12e), in document order. kind is file module namespace package class method property
 * field constructor enum interface function variable constant string number boolean array object
 * key null enum_member struct event operator type_parameter (NULL is function); depth is 0 at the
 * top level, one more for each level it is nested in; detail (a signature, a type) may be NULL.
 * Past 10000 or 1 MB the rest are left out. */
void rx_symbol(rx_symbols *l, rx_range r, const char *kind, int depth, const char *name, const char *detail);
/* A workspace edit (12g): start from a zeroed rx_wedit (rx_wedit e = {0}; in C++ rx_wedit e{};),
 * begin each file's edits with rx_wedit_file, then add them. They are made against one text, the
 * open document at version or with RX_ON_DISK the file on disk (rhun opens it), do not overlap,
 * and insertions at one position keep their order. rhun checks the whole edit before it changes
 * anything, and saves nothing. Past 4 MB, 1000 files or 100000 edits, or with a path over 4096
 * bytes, it cannot be sent (an edit cut short would leave files half changed): none of it is. */
#define RX_ON_DISK (-1)
void rx_wedit_file(rx_wedit *e, const char *path, rx_doc doc, int64_t version);
void rx_wedit_edit(rx_wedit *e, rx_range r, const char *text, size_t len);
/* nothing can be renamed there, and why (shown to the user): return rx_refuse(out, why); */
int rx_refuse(rx_wedit *e, const char *why);
/* Code actions (12f): kind quickfix (listed first), refactor, source or another word (NULL is
 * quickfix); title one line (an empty one leaves the action out); edit (NULL for none) is applied
 * when the action is chosen, and is used up here (one that cannot be sent, or needs a feature rhun
 * did not agree, leaves the action out);
 * data, len bytes of anything, comes back to the action handler. Past 1000 actions or 4 MB the rest
 * are left out. */
void rx_action(rx_actions *l, const char *kind, const char *title, rx_wedit *edit, const void *data, size_t len);
/* The bodies, appended to out, for answers built outside a handler; each empties its builder.
 * rx_wedit_finish returns 0, or -1 when the edit cannot be sent (e->fail and e->why say why) and
 * then appends nothing. rx_wedit_free drops an edit that is not finished. */
void rx_locations_finish(rx_locations *l, rx_buf *out);
void rx_symbols_finish(rx_symbols *l, rx_buf *out);
void rx_actions_finish(rx_actions *l, rx_buf *out);
int rx_wedit_finish(rx_wedit *e, rx_buf *out);
void rx_wedit_free(rx_wedit *e);

/* Writing a notebook body: rx_nb_begin with the number of cells, then each cell with the number of
 * its outputs, each output with the number of its items, each item. kind "code" or "markup"; lang a
 * grammar name ("-" none); count -1 for none; output kind stdout stderr result display error; id
 * NULL for none; item type text ansi png jpeg gif (w h the logical size, 0 0 natural). */
void rx_nb_begin(rx_buf *out, int cells, const void *meta, size_t meta_len);
void rx_nb_cell(rx_buf *out, const char *kind, const char *lang, int64_t count, const void *source,
                size_t source_len, const void *meta, size_t meta_len, int outputs);
void rx_nb_output(rx_buf *out, const char *kind, const char *id, const void *keep, size_t keep_len, int items);
void rx_nb_item(rx_buf *out, const char *type, int w, int h, const void *data, size_t len);
/* Reading one (nb-save's): each returns 1, or 0 on a body that cannot be read. */
typedef struct { const unsigned char *p, *end; int cells; const unsigned char *meta; size_t meta_len; } rx_nb_reader;
typedef struct { char kind[8]; char lang[130]; int64_t count; const unsigned char *source; size_t source_len;
                 const unsigned char *meta; size_t meta_len; int outputs; } rx_nb_cell_rec;
typedef struct { char kind[8]; char id[72]; const unsigned char *keep; size_t keep_len; int items; } rx_nb_output_rec;
typedef struct { char type[72]; int w, h; const unsigned char *data; size_t len; } rx_nb_item_rec;
int rx_nb_read_begin(rx_nb_reader *r, const unsigned char *body, size_t len);
int rx_nb_read_cell(rx_nb_reader *r, rx_nb_cell_rec *c);
int rx_nb_read_output(rx_nb_reader *r, rx_nb_output_rec *o);
int rx_nb_read_item(rx_nb_reader *r, rx_nb_item_rec *it);
/* A run's state (queued running done failed cancelled) and count (-1 leaves it); an output body
 * (built with rx_nb_output and rx_nb_item; op append, replace with target an index or an id, clear
 * and clear-wait with no body); stream text (stderr 0 or 1, cut in 64 KB messages); the kernel
 * button's text and state (idle busy starting dead none). 0 or -1 as the other requests. */
int rx_nb_state(rx_ext *ext, int64_t nb, int64_t cell, int64_t run, const char *state, int64_t count);
int rx_nb_output_send(rx_ext *ext, int64_t nb, int64_t cell, int64_t run, const char *op, const char *target,
                      const void *body, size_t len);
int rx_nb_stream(rx_ext *ext, int64_t nb, int64_t cell, int64_t run, int is_stderr, const void *data, size_t len);
int rx_nb_kernel_label(rx_ext *ext, int64_t nb, const char *state, const char *label);
/* A kernel for notebook nb, started as the user outside the sandbox (rhun asks the user the first
 * time in a project, so this may wait): program a name of [needs] kernels or an absolute path
 * whose last part is one, dir "" for the notebook's folder, args and NAME=VALUE env. Returns its
 * EXEC (its output goes to kernel-EXEC.log in $RHUN_STATE), or -1. rx_nb_exec_signal sends
 * "interrupt", "terminate" or "kill" to its process group. */
int64_t rx_nb_exec(rx_ext *ext, int64_t nb, const char *program, const char *dir, const char *const *args, int argc,
                   const char *const *env, int envc);
int rx_nb_exec_signal(rx_ext *ext, int64_t exec, const char *signal);
/* Custom editors: up to max (at most 4 MB) bytes of the editor's file from offset, malloc'd (free
 * it; *len 0 at its end), or NULL; whether an owned model differs from its file. */
unsigned char *rx_editor_read(rx_ext *ext, rx_doc cdoc, int64_t offset, size_t max, size_t *len);
int rx_editor_dirty(rx_ext *ext, rx_doc cdoc, int dirty);
/* editor-open's SOURCE while the editor_open handler runs: "file", or "backup" when rhun opens
 * unsaved changes it kept (rx_editor_read then reads the backup); "file" outside it. */
const char *rx_editor_source(const rx_ext *ext);
/* The command body of rx_nb_exec (and test-exec), onto out. */
void rx_command_body(rx_buf *out, const char *program, const char *dir, const char *const *args, int argc,
                     const char *const *env, int envc);

/* the start of the word (letters, digits, _, non-ASCII) that ends at `at` in text */
rx_pos rx_word_start(const unsigned char *text, size_t len, rx_pos at);
/* the rest of an event's line after its first n fields: rx_event_tail(ev, 3) is the path of
 * "opened DOC VERSION LANG PATH" (a path may hold spaces, which argv splits) */
const char *rx_event_tail(const rx_event *ev, int n);

/* Terminals and tasks (protocol 1.1): feature "terminals" runs programs in terminal tabs of rhun's
 * panel, feature "tasks" (agreed only with "terminals") publishes tasks rhun lists, runs with their
 * dependencies and turns into problems. [needs] terminal = run (input for rx_terminal_input,
 * output for rx_terminal_screen and output events). Programs run as the user after one approval
 * per extension and project. Lists are NULL-terminated arrays of strings, or NULL. */
typedef uint64_t rx_term;       /* a terminal tab it opened or one running its task; 0 none */
typedef struct {
    const char *shell;          /* a command line (/bin/sh -c; powershell.exe -Command), or NULL */
    const char *const *argv;    /* else the program (looked up on PATH without a slash) and its arguments */
    const char *cwd;            /* NULL or "": the project folder; relative: inside it */
    const char *const *env;     /* "NAME=value" */
    const char *const *unset;   /* NAME */
} rx_program;
typedef struct {
    const char *id;             /* [a-z0-9_.-]{1,64}, from a letter or digit */
    const char *label, *detail; /* detail NULL: the command */
    rx_program program;
    const char *group;          /* NULL, "build" or "test" */
    int is_default;             /* its group's default */
    const char *const *depends; /* task ids */
    int parallel;               /* the dependencies run together, else in order */
    const char *const *builtin_matchers;   /* go gcc rustc tsc msbuild */
    const char *const *matcher_texts;      /* inline matchers (rx_check_matcher) */
    const char *const *ready;   /* patterns: a background task, done when a line matches */
    const char *const *busy;    /* patterns: a new cycle began */
    const char *reveal;         /* NULL, "show", "focus" or "silent" */
    int output;                 /* terminal-output events (terminal = output) */
} rx_task;
typedef struct {
    const char *label, *detail;
    rx_program program;         /* shell and argv NULL: the user's interactive shell */
    const char *reveal;
    int output;
} rx_terminal_spec;
/* the definitions on the wire (0, or -1 with the reason in *why): checked as rhun checks them */
int rx_task_encode(const rx_task *t, rx_buf *out, const char **why);
int rx_terminal_encode(const rx_terminal_spec *s, rx_buf *out, const char **why);
int rx_set_tasks(rx_ext *ext, const rx_task *tasks, size_t n);
rx_term rx_start_task(rx_ext *ext, const char *id);               /* 0 and rx_error on failure */
rx_term rx_run_task(rx_ext *ext, const rx_task *t);
rx_term rx_open_terminal(rx_ext *ext, const rx_terminal_spec *s);
int rx_stop_terminal(rx_ext *ext, rx_term t);       /* Ctrl+C, then what is left is killed 2 s later */
int rx_restart_terminal(rx_ext *ext, rx_term t);
int rx_close_terminal(rx_ext *ext, rx_term t);
int rx_show_terminal(rx_ext *ext, rx_term t, int focus);
int rx_terminal_input(rx_ext *ext, rx_term t, const void *bytes, size_t len);
/* the body "ROWS COLS CURSOR_ROW CURSOR_COL\n" then the visible rows, one per line */
int rx_terminal_screen(rx_ext *ext, rx_term t, rx_buf *body);
typedef struct {
    char kind[8];               /* started ended output closed ready busy */
    rx_term term;
    int64_t pid;                /* started */
    char task[72];              /* started: "" for a terminal */
    char reason[8];             /* ended: exit stopped failed */
    int64_t code;               /* ended: -1 when it could not start */
    int64_t errors, warnings;   /* ended, ready */
    int64_t dropped;            /* output: lines left out since the last */
    const unsigned char *lines; /* output: finished lines, each ending with LF (the event's body) */
    size_t lines_len;
} rx_terminal_event;
/* 1 for terminal-started, -ended, -output, -closed, task-ready and task-busy, else 0 */
int rx_parse_terminal_event(const rx_event *ev, rx_terminal_event *out);
/* Problem patterns as rhun matches them: NULL when fine, else the reason */
const char *rx_check_pattern(const char *p, size_t len);
const char *rx_check_matcher(const char *text, size_t len);
/* 1 when the whole line matches; caps[k] = {start, length} of PATTERN_FIELDS k (file line col
 * endline endcol severity code message) and bit k of *has set for each field the pattern holds */
int rx_match_pattern(const char *p, size_t plen, const char *line, size_t llen, size_t caps[8][2], unsigned *has);
const char *rx_builtin_matcher(const char *name);   /* its text, or NULL */

/* Tests (feature "tests"). An item: kind group, file, suite or test; id unique and stable (1 to
 * 512 bytes, one line); path "" or absolute (or "#DOC"); range NULL for none (it needs a path);
 * flags NULL, "lazy" (rhun asks for its children with resolve_tests), "nodebug" or
 * "lazy,nodebug"; desc and error NULL for none. A bad item fails the list (fail, why). */
void rx_test_item(rx_test_items *l, const char *kind, const char *id, const char *label, const char *path,
                  const rx_range *range, const char *flags, const char *desc, const char *error);
int rx_test_refuse(rx_test_items *l, const char *why);         /* returns 1, for resolve_tests */
int rx_test_items_finish(rx_test_items *l, const char *parent, rx_buf *out);   /* 0, or -1: fail */
void rx_test_items_free(rx_test_items *l);
/* publishes the items under parent ("" the roots): replace makes them its only children, in this
 * order; otherwise they are added or updated. The list is used up. */
int rx_test_items_send(rx_ext *ext, const char *parent, rx_test_items *l, int replace);
int rx_test_remove(rx_ext *ext, const char *const *ids, int n);
/* A result record: item "" for the run itself (state NULL or "errored"); state queued, started,
 * passed, failed, errored, skipped, or NULL for output only; ms -1 for no duration. The extras
 * after it belong to it: a message starts a new one, location, expected and actual describe the
 * current message, output is appended to the item's (the run's) output. */
void rx_test_result(rx_test_results *r, const char *item, const char *state, long long ms);
void rx_test_message(rx_test_results *r, const char *text, size_t len);
void rx_test_location(rx_test_results *r, const char *path, rx_range range);
void rx_test_expected(rx_test_results *r, const void *data, size_t len);
void rx_test_actual(rx_test_results *r, const void *data, size_t len);
void rx_test_output(rx_test_results *r, const void *data, size_t len);
int rx_test_results_finish(rx_test_results *r, rx_buf *out);  /* 0, or -1: fail (syntax, toobig) */
void rx_test_results_free(rx_test_results *r);
/* sends the results (at most 2,000 records and 1 MiB a message: send as you go) and empties them */
int rx_test_update(rx_ext *ext, int64_t run, rx_test_results *r);
int rx_test_end(rx_ext *ext, int64_t run);                     /* the run is over */
/* lines from to to (one-based, inclusive) of the current file ran count times (0: not run) */
void rx_coverage_file(rx_test_coverage *c, const char *path);
void rx_coverage_lines(rx_test_coverage *c, int from, int to, long long count);
void rx_test_coverage_finish(rx_test_coverage *c, rx_buf *out);
void rx_test_coverage_free(rx_test_coverage *c);
int rx_test_coverage_send(rx_ext *ext, int64_t run, rx_test_coverage *c);      /* empties it */
/* starts a program as the user for the run: a bare name from [needs] tests, its folder (NULL or
 * "" for the project), argc arguments and envc NAME=VALUE variables (rhun may ask the user first;
 * the call waits). Its EXEC, or -1; its output and end come to test_out and test_exit. */
int64_t rx_test_exec(rx_ext *ext, int64_t run, const char *program, const char *dir,
                     const char *const *argv, int argc, const char *const *env, int envc);

#ifdef __cplusplus
}
#endif
#endif /* RHUNEXT_H */

/* ------------------------------------------------------------------------------------------ */
#ifdef RHUNEXT_IMPLEMENTATION

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#ifdef RHUNEXT_AUTH
#include <winsock2.h>
#include <windows.h>
#include <bcrypt.h>
#endif
#include <fcntl.h>
#include <io.h>
#elif defined(RHUNEXT_AUTH)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#define RX_MAXF 32
#define RX_BODY_MAX ((long long)1 << 30)     /* the longest reply body read (text, range, ask) */
#define RX_EVENT_BODY_MAX ((long long)4 << 20)  /* an edits or action event's: rhun sends less */
#define RX_REQUEST_BODY_MAX ((long long)64 << 10)   /* one of rhun's 1.1 requests' (resolve data, ...) */

typedef struct rx_msg {
    char *line;
    unsigned char *body;
    size_t body_len;
    struct rx_msg *next;
} rx_msg;

struct rx_ext {
    int64_t next_id;
    rx_msg *queue, *queue_tail;     /* messages that came while a request waited */
    char features[1024];
    char error[512];
    int gone;
    rx_apply_error apply_error;     /* the last rx_apply's refusal */
    int64_t query_id;               /* the request of rhun's being answered, and whether rhun */
    int query_cancelled;            /* withdrew it (query-stream) */
    const char *editor_source;      /* editor-open's SOURCE while editor_open runs */
};

void rx_buf_add(rx_buf *b, const void *data, size_t len) {
    if (b->len + len + 1 > b->cap) {
        /* bytes from the buffer itself move with it */
        const char *src = (const char *)data;
        int inside = b->data && src >= b->data && src < b->data + b->cap;
        size_t at = inside ? (size_t)(src - b->data) : 0;
        size_t cap = b->cap ? b->cap * 2 : 256;
        while (cap < b->len + len + 1) cap *= 2;
        char *d = (char *)realloc(b->data, cap);
        if (!d) abort();
        b->data = d;
        b->cap = cap;
        if (inside) data = d + at;
    }
    if (len) memcpy(b->data + b->len, data, len);
    b->len += len;
    b->data[b->len] = 0;
}

void rx_buf_str(rx_buf *b, const char *s) { rx_buf_add(b, s, strlen(s)); }

void rx_buf_fmt(rx_buf *b, const char *fmt, ...) {
    char small[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(small, sizeof small, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n < sizeof small) {
        rx_buf_add(b, small, (size_t)n);
        return;
    }
    char *big = (char *)malloc((size_t)n + 1);
    if (!big) abort();
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    rx_buf_add(b, big, (size_t)n);
    free(big);
}

void rx_buf_free(rx_buf *b) {
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

/* ---- the pipes */

static int rx_read_line(char **out) {
    rx_buf b = {NULL, 0, 0};
    int c;
    while ((c = getchar()) != EOF) {
        if (c == '\n') break;
        char ch = (char)c;
        rx_buf_add(&b, &ch, 1);
    }
    if (c == EOF && b.len == 0) {
        rx_buf_free(&b);
        return 0;
    }
    if (b.len && b.data[b.len - 1] == '\r') b.data[--b.len] = 0;
    if (!b.data) rx_buf_add(&b, "", 0);
    *out = b.data;
    return 1;
}

static unsigned char *rx_read_body(size_t n) {
    if (n > (size_t)RX_BODY_MAX) return NULL;
    unsigned char *body = (unsigned char *)malloc(n + 1);
    if (!body) abort();
    if (n && fread(body, 1, n, stdin) != n) {
        free(body);
        return NULL;
    }
    body[n] = 0;
    return body;
}

static void rx_write(const char *line, const void *body, size_t len) {
    fputs(line, stdout);
    fputc('\n', stdout);
    if (len) fwrite(body, 1, len, stdout);
    fflush(stdout);
}

static int rx_split(char *line, char **f) {
    int n = 0;
    char *p = line;
    while (*p && n < RX_MAXF) {
        while (*p == ' ') p++;
        if (!*p) break;
        f[n++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
    }
    return n;
}

/* the last space-separated field of a line as a body's length: a plain decimal of 1 to 10 digits
 * and at most max, else -1 (checked before anything is allocated) */
static long long rx_body_len(const char *line, long long max) {
    const char *s = strrchr(line, ' ');
    if (!s || !s[1] || strlen(s + 1) > 10) return -1;
    long long v = 0;
    for (s++; *s; s++) {
        if (*s < '0' || *s > '9') return -1;
        v = v * 10 + (*s - '0');
    }
    return v <= max ? v : -1;
}

/* the session is over: requests fail with why, and rx_run returns 1 once the queue is empty */
static void rx_gone(rx_ext *ext, const char *why) {
    ext->gone = 1;
    snprintf(ext->error, sizeof ext->error, "closed %s", why);
}

static const char rx_bad_len[] = "a body's length that is not a decimal of at most 10 digits within its cap";

/* the number of space-separated fields of a line */
static int rx_nfields(const char *line) {
    int n = 0;
    for (const char *p = line; *p;) {
        while (*p == ' ') p++;
        if (!*p) break;
        n++;
        while (*p && *p != ' ') p++;
    }
    return n;
}

/* the messages followed by a body, LEN their last field: the events edits (6a), action (12f),
 * selections, link, test-out, editor-path, retained views' (1.1) and run with command-args (0 run EXT.NAME DOC
 * FROM LEN), and the requests below */
static int rx_has_body(const char *line) {
    if (strncmp(line, "0 edits ", 8) == 0 || strncmp(line, "0 action ", 9) == 0 ||
        strncmp(line, "0 selections ", 13) == 0 || strncmp(line, "0 nb-run ", 9) == 0 ||
        strncmp(line, "0 link ", 7) == 0 || strncmp(line, "0 test-out ", 11) == 0 ||
        strncmp(line, "0 editor-path ", 14) == 0 || strncmp(line, "0 terminal-output ", 18) == 0 ||
        strncmp(line, "0 folders ", 10) == 0)
        return 1;
    if (strncmp(line, "0 run ", 6) == 0) return rx_nfields(line) == 6;    /* command-args */
    if (line[0] >= '1' && line[0] <= '9') {
        /* requests: ID commit-message ... LEN; vkids, vmore (the node id follows); tool and
         * agent-prompt (their input); tests-resolve and test-run (tests); nb-open and nb-save:
         * rx_host_body */
        static const char *const req[] = {"commit-message ", "vkids ", "vmore ", "tool ", "agent-prompt ",
                                          "tests-resolve ", "test-run ", "debug-resolve ", NULL};
        const char *v = strchr(line, ' ');
        if (!v) return 0;
        if (strncmp(v + 1, "call ", 5) == 0) return rx_nfields(line) == 6;   /* ID call EXT.NAME DOC CALLER LEN */
        for (int i = 0; req[i]; i++)
            if (strncmp(v + 1, req[i], strlen(req[i])) == 0) return 1;
        return 0;
    }
    if (strncmp(line, "0 quick ", 8) == 0) {
        /* quick input: "0 quick Q WHAT ... LEN" for value, active, accept and button */
        const char *w = strchr(line + 8, ' ');
        if (!w) return 0;
        w++;
        static const char *qwith[] = {"value ", "active ", "accept ", "button ", NULL};
        for (int i = 0; qwith[i]; i++)
            if (strncmp(w, qwith[i], strlen(qwith[i])) == 0) return 1;
        return 0;
    }
    if (strncmp(line, "0 view ", 7) != 0) return 0;
    /* retained views: "0 view VIEW WHAT ... LEN" for these WHATs */
    const char *w = strchr(line + 7, ' ');
    if (!w) return 0;
    w++;
    static const char *with[] = {"select ", "activate ", "expand ", "collapse ", "check ", "action ",
                                 "discarded ", "change ", "link ", "text ", "paste ", NULL};
    for (int i = 0; with[i]; i++)
        if (strncmp(w, with[i], strlen(with[i])) == 0) return 1;
    return 0;
}

/* the cap of a message's body (0 for none): RX_EVENT_BODY_MAX for rx_has_body's, and rhun's 1.1
 * requests that carry data; nb-open and nb-save (rx_host_body) have their own */
static long long rx_body_cap(const char *line) {
    static const char *const verbs[] = {"complete-resolve", "code-action-resolve", "calls-in", "calls-out",
                                        "supertypes", "subtypes", "workspace-symbols", "selection-ranges"};
    if (rx_has_body(line)) return RX_EVENT_BODY_MAX;
    const char *verb = strchr(line, ' ');
    if (line[0] < '1' || line[0] > '9' || !verb) return 0;
    verb++;
    for (size_t i = 0; i < sizeof verbs / sizeof verbs[0]; i++) {
        size_t n = strlen(verbs[i]);
        if (strncmp(verb, verbs[i], n) == 0 && verb[n] == ' ') return RX_REQUEST_BODY_MAX;
    }
    return 0;
}

/* whether a line is rhun's cancel of request id (a decimal), or its bye */
static int rx_cancels(const char *line, const char *id) {
    size_t n = strlen(id);
    if (strcmp(line, "0 bye") == 0 || strncmp(line, "0 bye ", 6) == 0) return 1;
    return strncmp(line, "0 cancel ", 9) == 0 && strncmp(line + 9, id, n) == 0 && line[9 + n] == 0;
}

/* a request from rhun followed by a body: nb-open and nb-save (ID VERB NB LEN), up to 64 MB */
static int rx_host_body(const char *line) {
    const char *sp = strchr(line, ' ');
    if (!sp || line[0] < '1' || line[0] > '9') return 0;
    return strncmp(sp + 1, "nb-open ", 8) == 0 || strncmp(sp + 1, "nb-save ", 8) == 0 ||
           strncmp(sp + 1, "editor-open ", 12) == 0;
}
#define RX_NB_BODY_MAX (((long long)64 << 20) + 65536)

/* reads the next message: a line, and a body when it is an edits or action event; NULL when the
 * session is over (end of input, or a length rhun cannot have sent: its body is not read) */
static rx_msg *rx_read_msg(rx_ext *ext) {
    char *line;
    if (!rx_read_line(&line)) {
        rx_gone(ext, "rhun went away");
        return NULL;
    }
    rx_msg *m = (rx_msg *)calloc(1, sizeof *m);
    if (!m) abort();
    m->line = line;
    long long cap = rx_host_body(line) ? RX_NB_BODY_MAX : rx_body_cap(line);
    if (cap) {
        long long n = rx_body_len(line, cap);
        if (n < 0 || !(m->body = rx_read_body((size_t)n))) {
            rx_gone(ext, n < 0 ? rx_bad_len : "rhun went away");
            free(line);
            free(m);
            return NULL;
        }
        m->body_len = (size_t)n;
    }
    return m;
}

static void rx_free_msg(rx_msg *m) {
    free(m->line);
    free(m->body);
    free(m);
}

const char *rx_error(rx_ext *ext) { return ext->error; }

int rx_has(rx_ext *ext, const char *feature) {
    size_t n = strlen(feature);
    const char *p = ext->features;
    while ((p = strstr(p, feature)) != NULL) {
        if ((p == ext->features || p[-1] == ' ') && (p[n] == 0 || p[n] == ' ')) return 1;
        p += n;
    }
    return 0;
}

int rx_request(rx_ext *ext, const char *verb_args, const void *body, size_t len, rx_buf *fields, rx_buf *body_out) {
    if (ext->gone) {
        snprintf(ext->error, sizeof ext->error, "closed rhun went away");
        return -1;
    }
    /* ids are 1 to 2147483647 (section 3); requests here wait one at a time, so wrapping is safe */
    if (ext->next_id >= 2147483647) ext->next_id = 0;
    int64_t id = ++ext->next_id;
    char head[64];
    snprintf(head, sizeof head, "%lld ", (long long)id);
    rx_buf line = {NULL, 0, 0};
    rx_buf_str(&line, head);
    rx_buf_str(&line, verb_args);
    rx_write(line.data, body, len);
    rx_buf_free(&line);
    int context_body = strncmp(verb_args, "snippet-context ", 16) == 0;
    int wants_body = strncmp(verb_args, "text ", 5) == 0 || strncmp(verb_args, "range ", 6) == 0 ||
                     strncmp(verb_args, "ask ", 4) == 0 || context_body ||
                     strncmp(verb_args, "secret-get ", 11) == 0 || strcmp(verb_args, "secret-keys") == 0 ||
                     strncmp(verb_args, "external-uri ", 13) == 0 || strcmp(verb_args, "link") == 0 ||
                     strncmp(verb_args, "setting-get ", 12) == 0 ||

                     strcmp(verb_args, "git-repo") == 0 || strcmp(verb_args, "git-status") == 0 ||
                     strncmp(verb_args, "git-blame ", 10) == 0 || strncmp(verb_args, "git-log ", 8) == 0 ||
                     strncmp(verb_args, "git-show ", 9) == 0 || strncmp(verb_args, "scm-input ", 10) == 0 ||
                     strcmp(verb_args, "tools") == 0 || strncmp(verb_args, "tool-call ", 10) == 0 ||
                     strncmp(verb_args, "editor-read ", 12) == 0 || strncmp(verb_args, "terminal-screen ", 16) == 0;
    int run_body = strncmp(verb_args, "run-command ", 12) == 0;
    wants_body = wants_body || run_body;
    char want[64];
    snprintf(want, sizeof want, "%lld", (long long)id);
    for (;;) {
        char *l;
        if (!rx_read_line(&l)) {
            rx_gone(ext, "rhun went away");
            return -1;
        }
        int is_ok = strncmp(l, "ok ", 3) == 0, is_err = strncmp(l, "error ", 6) == 0;
        if (is_ok || is_err) {
            const char *p = l + (is_ok ? 3 : 6);
            size_t n = strlen(want);
            if (strncmp(p, want, n) == 0 && (p[n] == ' ' || p[n] == 0)) {
                const char *rest = p[n] == ' ' ? p + n + 1 : p + n;
                if (is_err) {
                    snprintf(ext->error, sizeof ext->error, "%s", rest);
                    free(l);
                    return -1;
                }
                if (fields) rx_buf_str(fields, rest);
                if (wants_body) {
                    long long blen = rx_body_len(l, context_body ? (4u << 20) : run_body ? (1 << 20) : RX_BODY_MAX);
                    unsigned char *b = blen >= 0 ? rx_read_body((size_t)blen) : NULL;
                    if (!b) {
                        rx_gone(ext, blen < 0 ? rx_bad_len : "rhun went away");
                        free(l);
                        return -1;
                    }
                    if (body_out) rx_buf_add(body_out, b, (size_t)blen);
                    free(b);
                }
                free(l);
                return 0;
            }
            free(l);        /* a reply to nothing waiting: dropped */
            continue;
        }
        /* an event or a request from rhun: kept for later, in order */
        rx_msg *m = (rx_msg *)calloc(1, sizeof *m);
        if (!m) abort();
        m->line = l;
        if (ext->query_id) {
            char qid[32];
            snprintf(qid, sizeof qid, "%lld", (long long)ext->query_id);
            if (rx_cancels(l, qid)) ext->query_cancelled = 1;
        }
        long long cap = rx_host_body(l) ? RX_NB_BODY_MAX : rx_body_cap(l);
        if (cap) {
            long long blen = rx_body_len(l, cap);
            if (blen < 0 || !(m->body = rx_read_body((size_t)blen))) {
                rx_gone(ext, blen < 0 ? rx_bad_len : "rhun went away");
                rx_free_msg(m);
                return -1;
            }
            m->body_len = (size_t)blen;
        }
        if (ext->queue_tail) ext->queue_tail->next = m;
        else ext->queue = m;
        ext->queue_tail = m;
    }
}

static int64_t rx_req_num(rx_ext *ext, const char *verb_args, const void *body, size_t len) {
    rx_buf f = {NULL, 0, 0};
    int r = rx_request(ext, verb_args, body, len, &f, NULL);
    int64_t v = r < 0 ? -1 : (f.data ? atoll(f.data) : 0);
    rx_buf_free(&f);
    return v;
}

static int rx_req_body(rx_ext *ext, const char *verb, const char *text) {
    char line[64];
    size_t n = strlen(text);
    snprintf(line, sizeof line, "%s %zu", verb, n);
    return rx_request(ext, line, text, n, NULL, NULL);
}

int rx_toast(rx_ext *ext, const char *text) { return rx_req_body(ext, "toast", text); }
int rx_status(rx_ext *ext, const char *text) { return rx_req_body(ext, "status", text); }

unsigned char *rx_text(rx_ext *ext, rx_doc doc, size_t *len, int64_t *version) {
    char line[64];
    snprintf(line, sizeof line, "text %lld", (long long)doc);
    rx_buf f = {NULL, 0, 0}, b = {NULL, 0, 0};
    if (rx_request(ext, line, NULL, 0, &f, &b) < 0) {
        rx_buf_free(&f);
        rx_buf_free(&b);
        return NULL;
    }
    if (version) *version = f.data ? atoll(f.data) : 0;
    if (len) *len = b.len;
    rx_buf_free(&f);
    if (!b.data) rx_buf_add(&b, "", 0);
    return (unsigned char *)b.data;
}

int64_t rx_insert(rx_ext *ext, rx_doc doc, int64_t version, const char *text, size_t len) {
    char line[96];
    snprintf(line, sizeof line, "insert %lld %lld %zu", (long long)doc, (long long)version, len);
    return rx_req_num(ext, line, text, len);
}

int64_t rx_replace(rx_ext *ext, rx_doc doc, int64_t version, rx_range r, const char *text, size_t len) {
    char line[160];
    snprintf(line, sizeof line, "replace %lld %lld %d %d %d %d %zu", (long long)doc, (long long)version,
             r.start.line, r.start.col, r.end.line, r.end.col, len);
    return rx_req_num(ext, line, text, len);
}

int rx_select(rx_ext *ext, rx_doc doc, rx_range r) {
    char line[128];
    snprintf(line, sizeof line, "select %lld %d %d %d %d", (long long)doc, r.start.line, r.start.col, r.end.line, r.end.col);
    return rx_request(ext, line, NULL, 0, NULL, NULL);
}

rx_doc rx_open(rx_ext *ext, const char *path, rx_pos at) {
    rx_buf line = {NULL, 0, 0};
    rx_buf_fmt(&line, "open %d %d %s", at.line, at.col, path);
    rx_doc d = rx_req_num(ext, line.data, NULL, 0);
    rx_buf_free(&line);
    return d;
}

int rx_register(rx_ext *ext, const char *name, const char *keys, const char *title) {
    rx_buf line = {NULL, 0, 0};
    rx_buf_fmt(&line, "register %s %s %s", name, keys && *keys ? keys : "-", title);
    int r = rx_request(ext, line.data, NULL, 0, NULL, NULL);
    rx_buf_free(&line);
    return r;
}

int rx_unregister(rx_ext *ext, const char *name) {
    if (!rx_has(ext, "commands")) {
        snprintf(ext->error, sizeof ext->error, "unknown commands feature was not agreed");
        return -1;
    }
    size_t n = name ? strlen(name) : 0;
    if (!n || n > 40 || strspn(name, "abcdefghijklmnopqrstuvwxyz0123456789_") != n) {
        snprintf(ext->error, sizeof ext->error, "syntax invalid command name");
        return -1;
    }
    char line[64];
    snprintf(line, sizeof line, "unregister %s", name);
    return rx_request(ext, line, NULL, 0, NULL, NULL);
}

static int64_t rx_watch_id(const char *raw) {
    if (!raw || !*raw) return 0;
    uint64_t value = 0;
    size_t count = 0;
    while (*raw) {
        unsigned char c = (unsigned char)*raw++;
        if (c < '0' || c > '9' || ++count > 10) return 0;
        value = value * 10 + c - '0';
        if (value > 2147483647u) return 0;
    }
    return (int64_t)value;
}

int64_t rx_watch_workspace(rx_ext *ext) {
    if (!rx_has(ext, "workspace_watch")) {
        snprintf(ext->error, sizeof ext->error, "unknown workspace_watch was not negotiated");
        return -1;
    }
    rx_buf fields = {NULL, 0, 0};
    int result = rx_request(ext, "watch-workspace", NULL, 0, &fields, NULL);
    int64_t watch = result < 0 ? 0 : rx_watch_id(fields.data);
    rx_buf_free(&fields);
    if (result < 0) return -1;
    if (!watch) {
        snprintf(ext->error, sizeof ext->error, "syntax invalid workspace watch handle");
        return -1;
    }
    return watch;
}

int rx_busy(rx_ext *ext, int busy) {
    if (!rx_has(ext, "idle")) {
        snprintf(ext->error, sizeof ext->error, "unknown idle feature was not agreed");
        return -1;
    }
    return rx_request(ext, busy ? "busy yes" : "busy no", NULL, 0, NULL, NULL);
}

int rx_uri_valid(const char *uri) {
    const char *colon = uri ? strchr(uri, ':') : NULL;
    if (!colon || colon - uri < 2 || colon - uri > 32 || uri[0] < 'a' || uri[0] > 'z') return 0;
    for (const char *p = uri; p < colon; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '+' || *p == '.' || *p == '-')) return 0;
    size_t rest = strlen(colon + 1);
    return rest >= 1 && rest <= 4095 && !strpbrk(colon + 1, "\r\n");
}

int rx_content_changed(rx_ext *ext, const char *uri) {
    if (!rx_has(ext, "uris")) {
        snprintf(ext->error, sizeof ext->error, "unknown uris feature was not agreed");
        return -1;
    }
    if (!rx_uri_valid(uri)) {
        snprintf(ext->error, sizeof ext->error, "syntax invalid URI");
        return -1;
    }
    rx_buf line = {NULL, 0, 0};
    rx_buf_fmt(&line, "content-changed %s", uri);
    int r = rx_request(ext, line.data, NULL, 0, NULL, NULL);
    rx_buf_free(&line);
    return r;
}

int rx_open_url(rx_ext *ext, const char *url) {
    if (!rx_has(ext, "urls")) {
        snprintf(ext->error, sizeof ext->error, "unknown urls feature was not agreed");
        return -1;
    }
    size_t n = url ? strlen(url) : 0;
    int ok = n && n <= 8192 && (strncmp(url, "https://", 8) == 0 || strncmp(url, "http://", 7) == 0);
    for (size_t i = 0; ok && i < n; i++)
        if ((unsigned char)url[i] <= ' ' || url[i] == 0x7f) ok = 0;
    if (!ok) {
        snprintf(ext->error, sizeof ext->error, "syntax not an http or https URL");
        return -1;
    }
    char line[48];
    snprintf(line, sizeof line, "open-url %zu", n);
    rx_buf f = {NULL, 0, 0};
    int r = rx_request(ext, line, url, n, &f, NULL);
    int copied = r == 0 && f.data && strncmp(f.data, "copied", 6) == 0;
    rx_buf_free(&f);
    return r < 0 ? -1 : copied ? 2 : 1;
}

static char *rx_url_body(rx_ext *ext, const char *line, const char *body, size_t n) {
    if (!rx_has(ext, "urls")) {
        snprintf(ext->error, sizeof ext->error, "unknown urls feature was not agreed");
        return NULL;
    }
    rx_buf b = {NULL, 0, 0};
    if (rx_request(ext, line, body, n, NULL, &b) < 0) {
        rx_buf_free(&b);
        return NULL;
    }
    rx_buf_add(&b, "", 1);
    return b.data;
}

char *rx_external_uri(rx_ext *ext, const char *uri) {
    char line[48];
    size_t n = uri ? strlen(uri) : 0;
    snprintf(line, sizeof line, "external-uri %zu", n);
    return rx_url_body(ext, line, uri, n);
}

char *rx_link(rx_ext *ext) { return rx_url_body(ext, "link", NULL, 0); }

int rx_parse_link(const rx_event *event, int64_t *id, const char **link, size_t *len) {
    if (!event || !event->name || strcmp(event->name, "link") || event->argc != 2) return 0;
    int64_t v = atoll(event->argv[0]);
    if (v <= 0) return 0;
    if (id) *id = v;
    if (link) *link = event->body ? (const char *)event->body : "";
    if (len) *len = event->body_len;
    return 1;
}

#ifdef RHUNEXT_AUTH
void rx_sha256(const void *data, size_t len, unsigned char out[32]) {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    const unsigned char *p = (const unsigned char *)data;
    size_t total = len + 9 + (64 - (len + 9) % 64) % 64;
#define RX_ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
    for (size_t off = 0; off < total; off += 64) {
        unsigned char blk[64];
        for (int i = 0; i < 64; i++) {
            size_t j = off + i;
            if (j < len) blk[i] = p[j];
            else if (j == len) blk[i] = 0x80;
            else if (j >= total - 8) blk[i] = (unsigned char)(((uint64_t)len * 8) >> (8 * (total - 1 - j)));
            else blk[i] = 0;
        }
        uint32_t w[64], v[8];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t)blk[4 * i] << 24 | (uint32_t)blk[4 * i + 1] << 16 | (uint32_t)blk[4 * i + 2] << 8 | blk[4 * i + 3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = RX_ROR(w[i - 15], 7) ^ RX_ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = RX_ROR(w[i - 2], 17) ^ RX_ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        memcpy(v, h, sizeof v);
        for (int i = 0; i < 64; i++) {
            uint32_t t1 = v[7] + (RX_ROR(v[4], 6) ^ RX_ROR(v[4], 11) ^ RX_ROR(v[4], 25)) +
                          ((v[4] & v[5]) ^ (~v[4] & v[6])) + k[i] + w[i];
            uint32_t t2 = (RX_ROR(v[0], 2) ^ RX_ROR(v[0], 13) ^ RX_ROR(v[0], 22)) +
                          ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
            memmove(v + 1, v, 7 * sizeof v[0]);
            v[4] += t1;
            v[0] = t1 + t2;
        }
        for (int i = 0; i < 8; i++) h[i] += v[i];
    }
#undef RX_ROR
    for (int i = 0; i < 8; i++)
        out[4 * i] = (unsigned char)(h[i] >> 24), out[4 * i + 1] = (unsigned char)(h[i] >> 16),
        out[4 * i + 2] = (unsigned char)(h[i] >> 8), out[4 * i + 3] = (unsigned char)h[i];
}

static int rx_random(unsigned char *out, size_t n) {
#ifdef _WIN32
    return BCryptGenRandom(NULL, out, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0 ? 0 : -1;
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return -1;
    size_t got = fread(out, 1, n, f);
    fclose(f);
    return got == n ? 0 : -1;
#endif
}

int rx_random_hex(char *out, size_t nbytes) {
    unsigned char b[256];
    if (nbytes > sizeof b || rx_random(b, nbytes)) return -1;
    for (size_t i = 0; i < nbytes; i++) snprintf(out + 2 * i, 3, "%02x", b[i]);
    out[2 * nbytes] = 0;
    return 0;
}

static void rx_base64url(const unsigned char *d, size_t n, char *out) {
    static const char a[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < n ? (uint32_t)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        size_t chars = n - i >= 3 ? 4 : n - i + 1;
        for (size_t c = 0; c < chars; c++) out[o++] = a[(v >> (18 - 6 * c)) & 63];
    }
    out[o] = 0;
}

void rx_pkce_challenge(const char *verifier, char out[44]) {
    unsigned char sum[32];
    rx_sha256(verifier, strlen(verifier), sum);
    rx_base64url(sum, 32, out);
}

int rx_pkce_new(rx_pkce *p) {
    unsigned char b[32];
    if (rx_random(b, sizeof b)) return -1;
    rx_base64url(b, sizeof b, p->verifier);
    rx_pkce_challenge(p->verifier, p->challenge);
    return 0;
}

#ifdef _WIN32
#define RX_CLOSESOCK(fd) closesocket((SOCKET)(fd))
#else
#define RX_CLOSESOCK(fd) close((int)(fd))
#endif

int rx_loopback_listen(rx_loopback *lb) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) return -1;
#endif
    intptr_t fd = (intptr_t)socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(0x7f000001);
    socklen_t al = sizeof a;
    if (bind(fd, (struct sockaddr *)&a, sizeof a) || listen(fd, 8) || getsockname(fd, (struct sockaddr *)&a, &al)) {
        RX_CLOSESOCK(fd);
        return -1;
    }
    lb->fd = fd;
    lb->port = ntohs(a.sin_port);
    return 0;
}

void rx_loopback_redirect_uri(const rx_loopback *lb, char *out, size_t cap) {
    snprintf(out, cap, "http://127.0.0.1:%d/callback", lb->port);
}

void rx_loopback_close(rx_loopback *lb) {
    if (lb->fd >= 0) RX_CLOSESOCK(lb->fd);
    lb->fd = -1;
}

static int rx_hexval(int c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

char *rx_query_get(const char *query, const char *name) {
    size_t nl = strlen(name);
    for (const char *p = query; p && *p;) {
        const char *end = strchr(p, '&');
        if (!end) end = p + strlen(p);
        if ((size_t)(end - p) > nl && strncmp(p, name, nl) == 0 && p[nl] == '=') {
            char *out = (char *)malloc((size_t)(end - p));
            if (!out) abort();
            size_t o = 0;
            for (const char *q = p + nl + 1; q < end; q++) {
                if (*q == '%' && q + 2 < end && rx_hexval(q[1]) >= 0 && rx_hexval(q[2]) >= 0) {
                    out[o++] = (char)(rx_hexval(q[1]) * 16 + rx_hexval(q[2]));
                    q += 2;
                } else {
                    out[o++] = *q == '+' ? ' ' : *q;
                }
            }
            out[o] = 0;
            return out;
        }
        p = *end ? end + 1 : end;
    }
    return NULL;
}

int rx_loopback_wait(rx_loopback *lb, const char *state, int timeout_ms, char **query) {
    for (;;) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(lb->fd, &rd);
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000, tv.tv_usec = (timeout_ms % 1000) * 1000;
        if (select((int)lb->fd + 1, &rd, NULL, NULL, &tv) <= 0) return 0;
        intptr_t c = (intptr_t)accept(lb->fd, NULL, NULL);
        if (c < 0) return -1;
        char req[8192];
        size_t n = 0;
        while (n < sizeof req - 1) {
            FD_ZERO(&rd);
            FD_SET(c, &rd);
            tv.tv_sec = 5, tv.tv_usec = 0;
            if (select((int)c + 1, &rd, NULL, NULL, &tv) <= 0) break;
            int got = (int)recv(c, req + n, (int)(sizeof req - 1 - n), 0);
            if (got <= 0) break;
            n += (size_t)got;
            req[n] = 0;
            if (strstr(req, "\r\n\r\n")) break;
        }
        req[n] = 0;
        /* GET /callback?QUERY HTTP/1.1 */
        char *target = strchr(req, ' ');
        char *end = target ? strchr(target + 1, ' ') : NULL;
        int is_cb = end && strncmp(target + 1, "/callback", 9) == 0 && (target[10] == '?' || target + 10 == end);
        if (!is_cb) {
            const char nf[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send(c, nf, (int)(sizeof nf - 1), 0);
            RX_CLOSESOCK(c);
            continue;
        }
        *end = 0;
        const char *q = target[10] == '?' ? target + 11 : "";
        char *got_state = rx_query_get(q, "state"), *code = rx_query_get(q, "code"), *err = rx_query_get(q, "error");
        int ok = !err && code && got_state && strcmp(got_state, state) == 0;
        free(got_state), free(code), free(err);
        const char *body = ok ? "<!doctype html><title>Signed in</title><p>Signed in. You can close this tab and return to rhun."
                              : "<!doctype html><title>Sign-in failed</title><p>Sign-in failed. You can close this tab.";
        char head[160];
        int hl = snprintf(head, sizeof head, "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                          "Content-Length: %zu\r\nConnection: close\r\n\r\n", strlen(body));
        send(c, head, hl, 0);
        send(c, body, (int)strlen(body), 0);
        RX_CLOSESOCK(c);
        if (!ok) return -1;
        if (query) {
            *query = (char *)malloc(strlen(q) + 1);
            if (!*query) abort();
            strcpy(*query, q);
        }
        return 1;
    }
}
#endif /* RHUNEXT_AUTH */

char *rx_setting_get(rx_ext *ext, const char *key, int64_t folder, const char *lang) {
    if (!rx_has(ext, "scoped-settings")) {
        snprintf(ext->error, sizeof ext->error, "unknown scoped-settings feature was not agreed");
        return NULL;
    }
    if (!key || !*key || strpbrk(key, " \r\n") || strlen(key) > 200 || (lang && strlen(lang) > 200)) {
        snprintf(ext->error, sizeof ext->error, "syntax invalid setting key");
        return NULL;
    }
    char line[512], l[208];
    snprintf(l, sizeof l, "%s", lang && *lang ? lang : "-");
    for (char *p = l; *p; p++)
        if (*p == ' ') *p = '_';
    snprintf(line, sizeof line, "setting-get %s %lld %s", key, (long long)folder, l);
    rx_buf b = {NULL, 0, 0};
    if (rx_request(ext, line, NULL, 0, NULL, &b) < 0) {
        rx_buf_free(&b);
        return NULL;
    }
    rx_buf_add(&b, "", 1);
    return b.data;
}

int rx_parse_folders(const rx_event *event, rx_folder *out, int max) {
    if (!event || !event->name || strcmp(event->name, "folders") || !event->body) return -1;
    const char *p = (const char *)event->body, *end = p + event->body_len;
    const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p));
    if (!nl) return -1;
    int n = atoi(p);
    if (n < 0 || n > 16 || n > max) return -1;
    p = nl + 1;
    for (int i = 0; i < n; i++) {
        nl = (const char *)memchr(p, '\n', (size_t)(end - p));
        if (!nl) return -1;
        long long id, flags, nlen, plen;
        char loc[16], head[96];
        size_t hl = (size_t)(nl - p);
        if (hl >= sizeof head) return -1;
        memcpy(head, p, hl);
        head[hl] = 0;
        if (sscanf(head, "%lld %15s %lld %lld %lld", &id, loc, &flags, &nlen, &plen) != 5 || nlen < 0 || plen < 0)
            return -1;
        p = nl + 1;
        if (nlen + plen > end - p) return -1;
        out[i].id = id, out[i].flags = (int)flags;
        snprintf(out[i].location, sizeof out[i].location, "%s", loc);
        out[i].name = p, out[i].name_len = (size_t)nlen;
        out[i].path = p + nlen, out[i].path_len = (size_t)plen;
        p += nlen + plen;
    }
    return p == end ? n : -1;
}

int64_t rx_folder_of(rx_ext *ext, const char *path) {
    if (!rx_has(ext, "workspace-folders")) {
        snprintf(ext->error, sizeof ext->error, "unknown workspace-folders feature was not agreed");
        return -1;
    }
    if (!path || !*path || strpbrk(path, "\r\n") || strlen(path) > 4096) {
        snprintf(ext->error, sizeof ext->error, "syntax invalid path");
        return -1;
    }
    rx_buf line = {NULL, 0, 0};
    rx_buf_fmt(&line, "folder-of %s", path);
    int64_t v = rx_req_num(ext, line.data, NULL, 0);
    rx_buf_free(&line);
    return v;
}

/* ---- feature files ---- */

typedef struct {
    const char *p, *end;
} rx_framed;

static int rx_framed_nums(rx_framed *r, long long *v, int n) {
    const char *nl = (const char *)memchr(r->p, '\n', (size_t)(r->end - r->p));
    if (!nl) return -1;
    int got = 0;
    const char *s = r->p;
    while (s < nl) {
        if (got == n || *s < '0' || *s > '9') return -1;
        long long x = 0;
        int digits = 0;
        while (s < nl && *s >= '0' && *s <= '9') {
            if (++digits > 18) return -1;
            x = x * 10 + (*s++ - '0');
        }
        v[got++] = x;
        if (s < nl) {
            if (*s != ' ' || s + 1 == nl) return -1;
            s++;
        }
    }
    r->p = nl + 1;
    return got == n ? 0 : -1;
}

static const char *rx_framed_bytes(rx_framed *r, long long n) {
    if (n < 0 || n > r->end - r->p) return NULL;
    const char *s = r->p;
    r->p += n;
    return s;
}

/* a find-files batch to its callback: -1 when it does not parse */
static int rx_found_paths(const unsigned char *b, size_t len, int (*cb)(const rx_found_path *, int, void *),
                          void *user, int *more) {
    rx_framed r = {(const char *)b, (const char *)b + len};
    long long n;
    if (rx_framed_nums(&r, &n, 1) || n > 100000) return -1;
    rx_found_path *a = (rx_found_path *)calloc((size_t)n + 1, sizeof *a);
    if (!a) abort();
    for (long long i = 0; i < n; i++) {
        long long f[2];
        if (rx_framed_nums(&r, f, 2) || !(a[i].path = rx_framed_bytes(&r, f[1]))) {
            free(a);
            return -1;
        }
        a[i].folder = f[0];
        a[i].path_len = (size_t)f[1];
    }
    if (r.p != r.end) {
        free(a);
        return -1;
    }
    *more = cb ? cb(a, (int)n, user) : 1;
    free(a);
    return 0;
}

/* a search-text batch to its callback */
static int rx_found_files(const unsigned char *b, size_t len, int (*cb)(const rx_found_file *, int, void *),
                          void *user, int *more) {
    rx_framed r = {(const char *)b, (const char *)b + len};
    long long n;
    if (rx_framed_nums(&r, &n, 1) || n > 100000) return -1;
    rx_found_file *a = (rx_found_file *)calloc((size_t)n + 1, sizeof *a);
    if (!a) abort();
    int ok = 1;
    for (long long i = 0; ok && i < n; i++) {
        long long f[5];
        if (rx_framed_nums(&r, f, 5) || !(a[i].path = rx_framed_bytes(&r, f[3])) || f[4] > 20000) {
            ok = 0;
            break;
        }
        a[i].folder = f[0], a[i].doc = f[1], a[i].version = f[2], a[i].path_len = (size_t)f[3];
        rx_search_match *m = (rx_search_match *)calloc((size_t)f[4] + 1, sizeof *m);
        if (!m) abort();
        a[i].matches = m;
        a[i].nmatches = (int)f[4];
        for (long long j = 0; j < f[4]; j++) {
            long long v[5];
            if (rx_framed_nums(&r, v, 5) || !(m[j].text = rx_framed_bytes(&r, v[4]))) {
                ok = 0;
                break;
            }
            m[j].line = (int)v[0], m[j].col = (int)v[1], m[j].end = (int)v[2], m[j].pre = (int)v[3];
            m[j].text_len = (size_t)v[4];
        }
    }
    if (ok && r.p != r.end) ok = 0;
    if (ok) *more = cb ? cb(a, (int)n, user) : 1;
    for (long long i = 0; i < n; i++) free((void *)a[i].matches);
    free(a);
    return ok ? 0 : -1;
}

/* sends a find or search and reads until its reply: its batches go to take, other messages wait in
 * the queue as rx_request keeps them; returning 0 from take sends a cancel */
static int rx_files_run(rx_ext *ext, const char *verb, const rx_file_query *q, int flags, const void *body,
                        size_t len, int (*take)(rx_ext *, const unsigned char *, size_t, void *, int *), void *ctx,
                        rx_finished *out) {
    if (!rx_has(ext, "files")) {
        snprintf(ext->error, sizeof ext->error, "unknown files feature was not agreed");
        return -1;
    }
    if (ext->gone) {
        snprintf(ext->error, sizeof ext->error, "closed rhun went away");
        return -1;
    }
    if (ext->next_id >= 2147483646) ext->next_id = 0;
    int64_t id = ++ext->next_id;
    char line[160];
    snprintf(line, sizeof line, "%lld %s %lld %d %d %zu", (long long)id, verb, (long long)q->folder, q->max, flags, len);
    rx_write(line, body, len);
    char want[32], found[48];
    snprintf(want, sizeof want, "%lld", (long long)id);
    snprintf(found, sizeof found, "0 found %lld ", (long long)id);
    int cancelled = 0, failed = 0;
    for (;;) {
        char *l;
        if (!rx_read_line(&l)) {
            rx_gone(ext, "rhun went away");
            return -1;
        }
        if (strncmp(l, found, strlen(found)) == 0) {
            long long blen = rx_body_len(l, RX_EVENT_BODY_MAX);
            unsigned char *b = blen >= 0 ? rx_read_body((size_t)blen) : NULL;
            free(l);
            if (!b) {
                rx_gone(ext, blen < 0 ? rx_bad_len : "rhun went away");
                return -1;
            }
            int more = 1;
            if (!cancelled && !failed) {
                if (take(ext, b, (size_t)blen, ctx, &more) < 0) failed = 1, more = 0;
                if (!more) {
                    cancelled = 1;
                    char c[64];
                    if (ext->next_id >= 2147483646) ext->next_id = 0;
                    snprintf(c, sizeof c, "%lld cancel %lld", (long long)++ext->next_id, (long long)id);
                    rx_write(c, NULL, 0);
                }
            }
            free(b);
            continue;
        }
        int is_ok = strncmp(l, "ok ", 3) == 0, is_err = strncmp(l, "error ", 6) == 0;
        if (is_ok || is_err) {
            const char *p = l + (is_ok ? 3 : 6);
            size_t n = strlen(want);
            if (strncmp(p, want, n) == 0 && (p[n] == ' ' || p[n] == 0)) {
                const char *rest = p[n] == ' ' ? p + n + 1 : p + n;
                int r = 0;
                if (failed) {
                    snprintf(ext->error, sizeof ext->error, "syntax a batch that does not parse");
                    r = -1;
                } else if (is_err) {
                    snprintf(ext->error, sizeof ext->error, "%s", rest);
                    r = -1;
                } else if (out) {
                    memset(out, 0, sizeof *out);
                    char st[16] = "";
                    long long a = 0, b = 0, c = 0;
                    if (strcmp(verb, "find-files") == 0) sscanf(rest, "%lld %15s", &a, st);
                    else sscanf(rest, "%lld %lld %lld %15s", &a, &b, &c, st);
                    out->count = a, out->files = b, out->skipped = c;
                    snprintf(out->status, sizeof out->status, "%s", st);
                }
                free(l);
                return r;
            }
            free(l);        /* a cancel's reply, or a reply to nothing waiting */
            continue;
        }
        rx_msg *m = (rx_msg *)calloc(1, sizeof *m);
        if (!m) abort();
        m->line = l;
        if (rx_has_body(l)) {
            long long blen = rx_body_len(l, RX_EVENT_BODY_MAX);
            if (blen < 0 || !(m->body = rx_read_body((size_t)blen))) {
                rx_gone(ext, blen < 0 ? rx_bad_len : "rhun went away");
                rx_free_msg(m);
                return -1;
            }
            m->body_len = (size_t)blen;
        }
        if (ext->queue_tail) ext->queue_tail->next = m;
        else ext->queue = m;
        ext->queue_tail = m;
    }
}

static int rx_files_globs(const rx_file_query *q, rx_buf *b) {
    for (int k = 0; k < 2; k++) {
        const char *const *list = k ? q->exclude : q->include;
        int n = k ? q->nexclude : q->ninclude;
        for (int i = 0; i < n; i++) {
            if (!list[i] || !*list[i] || strpbrk(list[i], "\r\n")) return -1;
            rx_buf_add(b, k ? "-" : "+", 1);
            rx_buf_str(b, list[i]);
            rx_buf_add(b, "\n", 1);
        }
    }
    return 0;
}

typedef struct {
    int (*paths)(const rx_found_path *, int, void *);
    int (*files)(const rx_found_file *, int, void *);
    void *user;
} rx_files_ctx;

static int rx_take_paths(rx_ext *ext, const unsigned char *b, size_t len, void *ctx, int *more) {
    (void)ext;
    rx_files_ctx *c = (rx_files_ctx *)ctx;
    return rx_found_paths(b, len, c->paths, c->user, more);
}

static int rx_take_files(rx_ext *ext, const unsigned char *b, size_t len, void *ctx, int *more) {
    (void)ext;
    rx_files_ctx *c = (rx_files_ctx *)ctx;
    return rx_found_files(b, len, c->files, c->user, more);
}

int rx_find_files(rx_ext *ext, const rx_file_query *q, int (*batch)(const rx_found_path *paths, int n, void *user),
                  void *user, rx_finished *out) {
    rx_buf g = {NULL, 0, 0};
    if (rx_files_globs(q, &g)) {
        rx_buf_free(&g);
        snprintf(ext->error, sizeof ext->error, "syntax an empty glob or one with a line break");
        return -1;
    }
    rx_files_ctx c;
    c.paths = batch, c.files = NULL, c.user = user;
    int r = rx_files_run(ext, "find-files", q, q->flags, g.data, g.len, rx_take_paths, &c, out);
    rx_buf_free(&g);
    return r;
}

int rx_search_text(rx_ext *ext, const void *pattern, size_t len, int regex, const rx_file_query *q,
                   int (*batch)(const rx_found_file *files, int n, void *user), void *user, rx_finished *out) {
    if (!regex && (len == 0 || len > 4096 || memchr(pattern, '\n', len))) {
        snprintf(ext->error, sizeof ext->error, "syntax a literal is 1 to 4096 bytes without LF");
        return -1;
    }
    rx_buf g = {NULL, 0, 0};
    if (rx_files_globs(q, &g)) {
        rx_buf_free(&g);
        snprintf(ext->error, sizeof ext->error, "syntax an empty glob or one with a line break");
        return -1;
    }
    rx_buf b = {NULL, 0, 0};
    rx_buf_fmt(&b, "%zu %zu\n", len, g.len);
    rx_buf_add(&b, pattern, len);
    if (g.len) rx_buf_add(&b, g.data, g.len);
    rx_files_ctx c;
    c.paths = NULL, c.files = batch, c.user = user;
    int flags = regex ? (q->flags | 64) : (q->flags & ~64);
    int r = rx_files_run(ext, "search-text", q, flags, b.data, b.len, rx_take_files, &c, out);
    rx_buf_free(&g);
    rx_buf_free(&b);
    return r;
}

int rx_secret_key_valid(const char *key) {
    size_t n = key ? strlen(key) : 0;
    return n >= 1 && n <= 64 && strspn(key, "abcdefghijklmnopqrstuvwxyz0123456789_.-") == n;
}

static int rx_secret_check(rx_ext *ext, const char *key) {
    if (!rx_has(ext, "secrets")) {
        snprintf(ext->error, sizeof ext->error, "unknown secrets feature was not agreed");
        return -1;
    }
    if (key && !rx_secret_key_valid(key)) {
        snprintf(ext->error, sizeof ext->error, "syntax invalid secret key");
        return -1;
    }
    return 0;
}

int rx_secret_get(rx_ext *ext, const char *key, unsigned char **value, size_t *len) {
    if (rx_secret_check(ext, key ? key : "")) return -1;
    char line[96];
    snprintf(line, sizeof line, "secret-get %s", key);
    rx_buf f = {NULL, 0, 0}, b = {NULL, 0, 0};
    if (rx_request(ext, line, NULL, 0, &f, &b) < 0) {
        rx_buf_free(&f);
        rx_buf_free(&b);
        return -1;
    }
    int found = f.data && f.data[0] == '1';
    rx_buf_free(&f);
    if (!found) {
        rx_buf_free(&b);
        return 0;
    }
    if (!b.data) rx_buf_add(&b, "", 0);
    if (len) *len = b.len;
    if (value) *value = (unsigned char *)b.data;
    else rx_buf_free(&b);
    return 1;
}

int rx_secret_set(rx_ext *ext, const char *key, const void *value, size_t len) {
    if (rx_secret_check(ext, key ? key : "")) return -1;
    if (len > 8192) {
        snprintf(ext->error, sizeof ext->error, "toobig a value is at most 8192 bytes");
        return -1;
    }
    char line[112];
    snprintf(line, sizeof line, "secret-set %s %zu", key, len);
    return rx_request(ext, line, value, len, NULL, NULL);
}

int rx_secret_delete(rx_ext *ext, const char *key) {
    if (rx_secret_check(ext, key ? key : "")) return -1;
    char line[96];
    snprintf(line, sizeof line, "secret-delete %s", key);
    return rx_request(ext, line, NULL, 0, NULL, NULL);
}

char *rx_secret_keys(rx_ext *ext, size_t *len) {
    if (rx_secret_check(ext, NULL)) return NULL;
    rx_buf b = {NULL, 0, 0};
    if (rx_request(ext, "secret-keys", NULL, 0, NULL, &b) < 0) {
        rx_buf_free(&b);
        return NULL;
    }
    if (!b.data) rx_buf_add(&b, "", 0);
    if (len) *len = b.len;
    return b.data;
}

int rx_unwatch_workspace(rx_ext *ext, int64_t watch) {
    if (!rx_has(ext, "workspace_watch") || watch <= 0 || watch > 2147483647) {
        snprintf(ext->error, sizeof ext->error, "syntax invalid workspace watch");
        return -1;
    }
    char line[64];
    snprintf(line, sizeof line, "unwatch-workspace %lld", (long long)watch);
    return rx_request(ext, line, NULL, 0, NULL, NULL);
}

int rx_parse_workspace_watch(const rx_event *event, int64_t *watch, int *rescan) {
    if (!event || !event->name || strcmp(event->name, "watch") || event->argc != 2 ||
        !event->argv || !event->argv[1] || !watch || !rescan) return 0;
    int64_t id = rx_watch_id(event->argv[0]);
    if (!id) return 0;
    if (!strcmp(event->argv[1], "changed")) *rescan = 0;
    else if (!strcmp(event->argv[1], "rescan")) *rescan = 1;
    else return 0;
    *watch = id;
    return 1;
}

/* ---- source control (protocol 1.1): git-data and commit-message ---- */

/* a reader of length-framed bodies: header words, LF, then raw bytes */
typedef struct { const char *b; size_t len, pos; int bad; } rx_fr;

static const char *rx_fr_word(rx_fr *r, size_t *n) {
    if (r->pos < r->len && r->b[r->pos] == ' ') r->pos++;
    size_t s = r->pos;
    while (r->pos < r->len && r->b[r->pos] != ' ' && r->b[r->pos] != '\n') r->pos++;
    *n = r->pos - s;
    if (!*n) r->bad = 1;
    return r->b + s;
}
static int64_t rx_fr_num(rx_fr *r) {
    size_t n;
    const char *w = rx_fr_word(r, &n);
    if (!n || n > 18) { r->bad = 1; return 0; }
    int64_t v = 0;
    for (size_t i = 0; i < n; i++) {
        if (w[i] < '0' || w[i] > '9') { r->bad = 1; return 0; }
        v = v * 10 + (w[i] - '0');
    }
    return v;
}
static void rx_fr_eol(rx_fr *r) {
    if (r->pos < r->len && r->b[r->pos] == '\n') r->pos++;
    else r->bad = 1;
}
static const char *rx_fr_bytes(rx_fr *r, int64_t n) {
    if (r->bad || n < 0 || (uint64_t)n > r->len - r->pos) { r->bad = 1; return ""; }
    const char *p = r->b + r->pos;
    r->pos += (size_t)n;
    return p;
}
static void rx_fr_sha(rx_fr *r, char out[72]) {
    size_t n;
    const char *w = rx_fr_word(r, &n);
    if (n >= 72) { r->bad = 1; n = 0; }
    memcpy(out, w, n);
    out[n] = 0;
}
static int rx_fr_done(const rx_fr *r) { return !r->bad && r->pos == r->len ? 0 : -1; }

static int rx_git_ok(rx_ext *ext) {
    if (rx_has(ext, "git-data")) return 1;
    snprintf(ext->error, sizeof ext->error, "unknown git-data was not negotiated");
    return 0;
}

int rx_valid_rev(const char *rev) {
    size_t n = rev ? strlen(rev) : 0;
    if (!n || n > 200 || rev[0] == '-') return 0;
    for (size_t i = 0; i < n; i++) {
        char c = rev[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("._/~^@{}+-", c))) return 0;
    }
    return 1;
}

int rx_parse_git_repo(rx_git_repo_info *o) {
    rx_fr r = {o->body.data, o->body.len, 0, 0};
    int64_t rl = rx_fr_num(&r), bl = rx_fr_num(&r), ul = rx_fr_num(&r);
    size_t n;
    const char *w = rx_fr_word(&r, &n);
    if (n >= sizeof o->head) return -1;
    memcpy(o->head, w, n), o->head[n] = 0;
    o->ahead = rx_fr_num(&r), o->behind = rx_fr_num(&r);
    w = rx_fr_word(&r, &n);
    if (n >= sizeof o->state) return -1;
    memcpy(o->state, w, n), o->state[n] = 0;
    rx_fr_eol(&r);
    o->root = rx_fr_bytes(&r, rl), o->root_len = (size_t)rl;
    o->branch = rx_fr_bytes(&r, bl), o->branch_len = (size_t)bl;
    o->upstream = rx_fr_bytes(&r, ul), o->upstream_len = (size_t)ul;
    if (strcmp(o->head, "branch") && strcmp(o->head, "detached") && strcmp(o->head, "unborn")) return -1;
    if (strcmp(o->state, "clean") && strcmp(o->state, "dirty") && strcmp(o->state, "merging") && strcmp(o->state, "rebasing")) return -1;
    return rx_fr_done(&r);
}

int rx_parse_git_status(rx_git_status_list *o) {
    rx_fr r = {o->body.data, o->body.len, 0, 0};
    int64_t n = rx_fr_num(&r);
    rx_fr_eol(&r);
    if (r.bad || n < 0 || (uint64_t)n > r.len) return -1;
    o->entries = (rx_git_status_entry *)calloc((size_t)n + 1, sizeof *o->entries);
    if (!o->entries) abort();
    for (o->n = 0; (int64_t)o->n < n && !r.bad; o->n++) {
        size_t xl;
        const char *xy = rx_fr_word(&r, &xl);
        int64_t pl = rx_fr_num(&r);
        rx_fr_eol(&r);
        if (xl != 2) return -1;
        rx_git_status_entry *e = &o->entries[o->n];
        e->x = xy[0], e->y = xy[1];
        e->path = rx_fr_bytes(&r, pl), e->path_len = (size_t)pl;
    }
    return rx_fr_done(&r);
}

int rx_parse_blame(rx_blame *o) {
    rx_fr r = {o->body.data, o->body.len, 0, 0};
    int64_t nc = rx_fr_num(&r), nr = rx_fr_num(&r);
    rx_fr_eol(&r);
    if (r.bad || (uint64_t)nc > r.len || (uint64_t)nr > r.len) return -1;
    o->commits = (rx_blame_commit *)calloc((size_t)nc + 1, sizeof *o->commits);
    o->runs = (rx_blame_run *)calloc((size_t)nr + 1, sizeof *o->runs);
    if (!o->commits || !o->runs) abort();
    for (o->ncommits = 0; (int64_t)o->ncommits < nc && !r.bad; o->ncommits++) {
        rx_blame_commit *c = &o->commits[o->ncommits];
        rx_fr_sha(&r, c->sha);
        c->time = rx_fr_num(&r);
        int64_t al = rx_fr_num(&r), ml = rx_fr_num(&r), sl = rx_fr_num(&r);
        rx_fr_eol(&r);
        c->author = rx_fr_bytes(&r, al), c->author_len = (size_t)al;
        c->mail = rx_fr_bytes(&r, ml), c->mail_len = (size_t)ml;
        c->summary = rx_fr_bytes(&r, sl), c->summary_len = (size_t)sl;
    }
    for (o->nruns = 0; (int64_t)o->nruns < nr && !r.bad; o->nruns++) {
        rx_blame_run *u = &o->runs[o->nruns];
        rx_fr_sha(&r, u->sha);
        u->l1 = rx_fr_num(&r), u->l2 = rx_fr_num(&r), u->orig = rx_fr_num(&r);
        rx_fr_eol(&r);
    }
    return rx_fr_done(&r);
}

int rx_parse_log(rx_log *o) {
    rx_fr r = {o->body.data, o->body.len, 0, 0};
    int64_t n = rx_fr_num(&r);
    rx_fr_eol(&r);
    if (r.bad || (uint64_t)n > r.len) return -1;
    o->entries = (rx_log_entry *)calloc((size_t)n + 1, sizeof *o->entries);
    if (!o->entries) abort();
    for (o->n = 0; (int64_t)o->n < n && !r.bad; o->n++) {
        rx_log_entry *e = &o->entries[o->n];
        rx_fr_sha(&r, e->sha);
        size_t pl;
        const char *p = rx_fr_word(&r, &pl);
        if (pl == 1 && p[0] == '-') pl = 0;
        e->parents = p, e->parents_len = pl;
        e->time = rx_fr_num(&r);
        int64_t al = rx_fr_num(&r), sl = rx_fr_num(&r);
        rx_fr_eol(&r);
        e->author = rx_fr_bytes(&r, al), e->author_len = (size_t)al;
        e->subject = rx_fr_bytes(&r, sl), e->subject_len = (size_t)sl;
    }
    return rx_fr_done(&r);
}

int rx_parse_commit_message(int64_t id, const unsigned char *body, size_t len, rx_commit_message_ask *o) {
    rx_fr r = {(const char *)(body ? body : (const unsigned char *)""), len, 0, 0};
    int64_t bl = rx_fr_num(&r), dl = rx_fr_num(&r);
    rx_fr_eol(&r);
    o->id = id;
    o->branch = rx_fr_bytes(&r, bl), o->branch_len = (size_t)bl;
    o->diff = rx_fr_bytes(&r, dl), o->diff_len = (size_t)dl;
    return rx_fr_done(&r);
}

int rx_check_commit_message(const char *m, size_t n, size_t *start, size_t *len) {
    size_t a = 0, b = n;
    while (a < b && (m[a] == ' ' || m[a] == '\t' || m[a] == '\r' || m[a] == '\n')) a++;
    while (b > a && (m[b - 1] == ' ' || m[b - 1] == '\t' || m[b - 1] == '\r' || m[b - 1] == '\n')) b--;
    *start = a, *len = b - a;
    if (b == a || b - a > 8192) return -1;
    for (size_t i = a; i < b; i++) {
        unsigned char c = (unsigned char)m[i];
        if (c == 0x7f || (c < 0x20 && c != '\t' && c != '\r' && c != '\n')) return -1;
    }
    return 0;
}

int rx_is_git_changed(const rx_event *ev) {
    return ev && ev->name && strcmp(ev->name, "git") == 0 && ev->argc == 1 && strcmp(ev->argv[0], "changed") == 0;
}

/* a git-data request whose body (when ok) goes to body, decoded by parse */
static int rx_git_req(rx_ext *ext, const char *line, const char *path, rx_buf *body) {
    if (!rx_git_ok(ext)) return -1;
    return rx_request(ext, line, path, path ? strlen(path) : 0, NULL, body);
}

int rx_git_repo(rx_ext *ext, rx_git_repo_info *out) {
    memset(out, 0, sizeof *out);
    if (rx_git_req(ext, "git-repo", NULL, &out->body) < 0) return -1;
    if (rx_parse_git_repo(out) == 0) return 0;
    snprintf(ext->error, sizeof ext->error, "syntax invalid git-repo reply");
    return -1;
}

int rx_git_status(rx_ext *ext, rx_git_status_list *out) {
    memset(out, 0, sizeof *out);
    if (rx_git_req(ext, "git-status", NULL, &out->body) < 0) return -1;
    if (rx_parse_git_status(out) == 0) return 0;
    snprintf(ext->error, sizeof ext->error, "syntax invalid git-status reply");
    return -1;
}

int rx_git_blame(rx_ext *ext, rx_doc doc, rx_blame *out) {
    memset(out, 0, sizeof *out);
    char line[64];
    snprintf(line, sizeof line, "git-blame %lld", (long long)doc);
    if (rx_git_req(ext, line, NULL, &out->body) < 0) return -1;
    if (rx_parse_blame(out) == 0) return 0;
    snprintf(ext->error, sizeof ext->error, "syntax invalid git-blame reply");
    return -1;
}

int rx_git_log(rx_ext *ext, const char *path, int l1, int l2, int max, rx_log *out) {
    memset(out, 0, sizeof *out);
    char line[96];
    snprintf(line, sizeof line, "git-log %d %d %d %zu", max, l1, l2, path ? strlen(path) : (size_t)0);
    if (rx_git_req(ext, line, path, &out->body) < 0) return -1;
    if (rx_parse_log(out) == 0) return 0;
    snprintf(ext->error, sizeof ext->error, "syntax invalid git-log reply");
    return -1;
}

int rx_git_show(rx_ext *ext, const char *rev, const char *path, rx_buf *out) {
    if (!rx_valid_rev(rev) || !path) {
        snprintf(ext->error, sizeof ext->error, "syntax invalid revision");
        return -1;
    }
    char line[256];
    snprintf(line, sizeof line, "git-show %s %zu", rev, strlen(path));
    return rx_git_req(ext, line, path, out);
}

int rx_git_diff_open(rx_ext *ext, const char *rev1, const char *rev2, const char *path) {
    if (!rev2) rev2 = "-";
    if (!rx_valid_rev(rev1) || (strcmp(rev2, "-") && !rx_valid_rev(rev2)) || !path) {
        snprintf(ext->error, sizeof ext->error, "syntax invalid revision");
        return -1;
    }
    char line[512];
    snprintf(line, sizeof line, "git-diff-open %s %s %zu", rev1, rev2, strlen(path));
    if (!rx_git_ok(ext)) return -1;
    return rx_request(ext, line, path, strlen(path), NULL, NULL);
}

void rx_git_repo_free(rx_git_repo_info *r) { rx_buf_free(&r->body); memset(r, 0, sizeof *r); }
void rx_git_status_free(rx_git_status_list *s) { free(s->entries); rx_buf_free(&s->body); memset(s, 0, sizeof *s); }
void rx_blame_free(rx_blame *b) { free(b->commits); free(b->runs); rx_buf_free(&b->body); memset(b, 0, sizeof *b); }
void rx_log_free(rx_log *l) { free(l->entries); rx_buf_free(&l->body); memset(l, 0, sizeof *l); }

static int rx_debug_under(const char *path, const char *prefix) {
    size_t n = strlen(prefix);
    return strncmp(path, prefix, n) == 0 && (path[n] == 0 || path[n] == '.');
}

void rx_debug_config_remove(rx_debug_config *c, const char *path) {
    size_t j = 0;
    for (size_t i = 0; i < c->n; i++) {
        if (rx_debug_under(c->entries[i].path, path)) {
            free(c->entries[i].path);
            free(c->entries[i].value);
        } else {
            c->entries[j++] = c->entries[i];
        }
    }
    c->n = j;
}

static int rx_debug_leaf_ok(char kind, const char *path, size_t plen, const char *value, size_t vlen) {
    size_t dots = 0;
    for (size_t i = 0; i < plen; i++) dots += path[i] == '.';
    if (plen == 0 || plen > 256 || dots >= 16 || vlen > 65536) return 0;
    if (kind == 's') return 1;
    if (kind == 'z') return vlen == 0;
    if (kind == 'b') return (vlen == 4 && !memcmp(value, "true", 4)) || (vlen == 5 && !memcmp(value, "false", 5));
    if (kind != 'n' || vlen == 0) return 0;
    size_t i = value[0] == '-';
    if (i == vlen || vlen - i > 16) return 0;
    for (; i < vlen; i++) if (value[i] < '0' || value[i] > '9') return 0;
    long long v = strtoll(value, NULL, 10);
    return v < (1LL << 53) && v > -(1LL << 53);
}

static int rx_debug_add(rx_debug_config *c, char kind, const char *path, size_t plen, const char *value, size_t vlen) {
    if (c->n >= 4096) return -1;
    if (c->n == c->cap) {
        size_t cap = c->cap ? c->cap * 2 : 8;
        rx_debug_entry *e = (rx_debug_entry *)realloc(c->entries, cap * sizeof *e);
        if (!e) abort();
        c->entries = e;
        c->cap = cap;
    }
    rx_debug_entry *e = &c->entries[c->n++];
    e->kind = kind;
    e->path = (char *)malloc(plen + 1);
    e->value = (char *)malloc(vlen + 1);
    if (!e->path || !e->value) abort();
    memcpy(e->path, path, plen);
    e->path[plen] = 0;
    memcpy(e->value, value, vlen);
    e->value[vlen] = 0;
    e->value_len = vlen;
    return 0;
}

int rx_debug_config_set(rx_debug_config *c, char kind, const char *path, const char *value) {
    if (!value) value = "";
    if (!rx_debug_leaf_ok(kind, path, strlen(path), value, strlen(value))) return -1;
    rx_debug_config_remove(c, path);
    return rx_debug_add(c, kind, path, strlen(path), value, strlen(value));
}

const rx_debug_entry *rx_debug_config_get(const rx_debug_config *c, const char *path) {
    for (size_t i = 0; i < c->n; i++)
        if (strcmp(c->entries[i].path, path) == 0) return &c->entries[i];
    return NULL;
}

void rx_debug_config_free(rx_debug_config *c) {
    for (size_t i = 0; i < c->n; i++) {
        free(c->entries[i].path);
        free(c->entries[i].value);
    }
    free(c->entries);
    memset(c, 0, sizeof *c);
}

int rx_debug_config_encode(const rx_debug_config *c, rx_buf *out) {
    size_t start = out->len;
    rx_buf_fmt(out, "%zu\n", c->n);
    for (size_t i = 0; i < c->n; i++) {
        const rx_debug_entry *e = &c->entries[i];
        rx_buf_fmt(out, "%c %zu %zu\n", e->kind, strlen(e->path), e->value_len);
        rx_buf_str(out, e->path);
        rx_buf_add(out, e->value, e->value_len);
    }
    return out->len - start > (1u << 20) ? -1 : 0;
}

static int rx_debug_num(const unsigned char **p, const unsigned char *end, char stop, size_t *v) {
    size_t n = 0;
    int digits = 0;
    while (*p < end && **p >= '0' && **p <= '9' && digits < 15) {
        n = n * 10 + (size_t)(**p - '0');
        (*p)++;
        digits++;
    }
    if (!digits || *p >= end || **p != stop) return 0;
    (*p)++;
    *v = n;
    return 1;
}

int rx_debug_config_parse(const unsigned char *body, size_t len, rx_debug_config *out, rx_debug_context *ctx) {
    const unsigned char *p = body, *end = body + len;
    size_t n, pl, vl;
    memset(out, 0, sizeof *out);
    if (ctx) ctx->doc = 0;
    if (!rx_debug_num(&p, end, '\n', &n) || n > 4096) return -1;
    for (size_t i = 0; i < n; i++) {
        if (end - p < 2 || p[1] != ' ') goto bad;
        char kind = (char)p[0];
        p += 2;
        if (!rx_debug_num(&p, end, ' ', &pl) || !rx_debug_num(&p, end, '\n', &vl)) goto bad;
        if (pl > (size_t)(end - p) || vl > (size_t)(end - p) - pl) goto bad;
        const char *path = (const char *)p, *value = (const char *)p + pl;
        if (!rx_debug_leaf_ok(kind, path, pl, value, vl) || memchr(path, 0, pl)) goto bad;
        rx_debug_add(out, kind, path, pl, value, vl);
        p += pl + vl;
    }
    while (p < end) {
        const unsigned char *nl = (const unsigned char *)memchr(p, '\n', (size_t)(end - p));
        if (!nl) goto bad;
        char line[128];
        size_t ll = (size_t)(nl - p);
        if (ll >= sizeof line) goto bad;
        memcpy(line, p, ll);
        line[ll] = 0;
        p = nl + 1;
        const char *last = strrchr(line, ' ');
        size_t size = (size_t)strtoull(last ? last + 1 : line, NULL, 10);
        if (size > (size_t)(end - p)) goto bad;
        long long d, l, col;
        unsigned long long sz;
        if (ctx && sscanf(line, "context %lld %lld %lld %llu", &d, &l, &col, &sz) == 4) {
            ctx->doc = d;
            ctx->at.line = (int)l;
            ctx->at.col = (int)col;
            ctx->path.len = 0;
            rx_buf_add(&ctx->path, p, size);
        }
        p += size;
    }
    return 0;
bad:
    rx_debug_config_free(out);
    return -1;
}

int64_t rx_debug_start(rx_ext *ext, const char *name, const rx_debug_config *c) {
    if (!rx_has(ext, "debug")) {
        snprintf(ext->error, sizeof ext->error, "unknown debug was not negotiated");
        return -1;
    }
    size_t nl = strlen(name);
    if (nl == 0 || nl > 256 || strchr(name, '\n')) {
        snprintf(ext->error, sizeof ext->error, "syntax a session name is 1 to 256 bytes on one line");
        return -1;
    }
    rx_buf body = {NULL, 0, 0};
    rx_buf_add(&body, name, nl);
    if (rx_debug_config_encode(c, &body) < 0) {
        rx_buf_free(&body);
        snprintf(ext->error, sizeof ext->error, "toobig the configuration is over 1 MB");
        return -1;
    }
    char line[64];
    snprintf(line, sizeof line, "debug-start %zu %zu", nl, body.len);
    int64_t id = rx_req_num(ext, line, body.data, body.len);
    rx_buf_free(&body);
    return id;
}

int rx_debug_stop(rx_ext *ext, int64_t session) {
    if (!rx_has(ext, "debug")) {
        snprintf(ext->error, sizeof ext->error, "unknown debug was not negotiated");
        return -1;
    }
    if (session <= 0) {
        snprintf(ext->error, sizeof ext->error, "syntax a session id is a positive number");
        return -1;
    }
    char line[64];
    snprintf(line, sizeof line, "debug-stop %lld", (long long)session);
    return rx_request(ext, line, NULL, 0, NULL, NULL);
}

int rx_parse_debug_ended(const rx_event *ev, int64_t *session, int *code, int *has_code) {
    if (strcmp(ev->name, "debug-ended") != 0 || ev->argc != 2) return 0;
    long long s = atoll(ev->argv[0]);
    if (s <= 0) return 0;
    *session = s;
    *has_code = strcmp(ev->argv[1], "-") != 0;
    *code = *has_code ? atoi(ev->argv[1]) : 0;
    return 1;
}

int rx_quickdiff_refresh(rx_ext *ext, const char *path) {
    if (!rx_has(ext, "quickdiff")) {
        snprintf(ext->error, sizeof ext->error, "unknown quickdiff was not negotiated");
        return -1;
    }
    if (!path) path = "-";
    char line[64];
    snprintf(line, sizeof line, "quickdiff-refresh %zu", strlen(path));
    return rx_request(ext, line, path, strlen(path), NULL, NULL);
}

/* ---- commands (protocol 1.1): command-args, context-keys, status-items ---- */

static int rx_fail(rx_ext *ext, const char *why) {
    snprintf(ext->error, sizeof ext->error, "%s", why);
    return -1;
}

static int rx_command_name_ok(const char *name) {
    size_t n = name ? strlen(name) : 0;
    return n && n <= 40 && strspn(name, "abcdefghijklmnopqrstuvwxyz0123456789_") == n;
}

int rx_menu_target(const rx_invocation *inv, const char **target, size_t *target_len,
                   const unsigned char **args, size_t *args_len) {
    if (!inv->from || strcmp(inv->from, "menu") != 0) return -1;
    const unsigned char *b = inv->body ? inv->body : (const unsigned char *)"";
    const unsigned char *lf = inv->body_len ? (const unsigned char *)memchr(b, '\n', inv->body_len) : NULL;
    size_t tl = lf ? (size_t)(lf - b) : inv->body_len;
    if (target) *target = (const char *)b;
    if (target_len) *target_len = tl;
    if (args) *args = lf ? lf + 1 : b + inv->body_len;
    if (args_len) *args_len = lf ? inv->body_len - tl - 1 : 0;
    return 0;
}

int rx_command_ref(rx_buf *out, const char *name, const void *args, size_t len) {
    if (!rx_command_name_ok(name) || len > 8192 || (len && !args)) return -1;
    rx_buf_fmt(out, "command %s %zu\n", name, len);
    rx_buf_add(out, args ? args : "", len);
    return 0;
}

int rx_run_command(rx_ext *ext, const char *name, const void *args, size_t len, rx_buf *result) {
    if (!rx_has(ext, "command-args")) return rx_fail(ext, "unknown command-args was not agreed");
    size_t n = name ? strlen(name) : 0;
    if (!n || n > 200 || strpbrk(name, " \r\n")) return rx_fail(ext, "syntax not a command name");
    if (len > 65536) return rx_fail(ext, "toobig run-command args are at most 65536 bytes");
    rx_buf line = {NULL, 0, 0};
    rx_buf_fmt(&line, "run-command %s %zu", name, len);
    int r = rx_request(ext, line.data, args, len, NULL, result);
    rx_buf_free(&line);
    return r;
}

int rx_plain_text(const char *s, size_t n) {
    const unsigned char *p = (const unsigned char *)s, *end = p + n;
    while (p < end) {
        uint32_t c = *p;
        int k = c < 0x80 ? 0 : (c & 0xe0) == 0xc0 ? 1 : (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 ? 3 : -1;
        if (k < 0 || end - p <= k) return 0;
        if (k) c &= 0x3f >> k;
        for (int i = 1; i <= k; i++) {
            if ((p[i] & 0xc0) != 0x80) return 0;
            c = c << 6 | (p[i] & 0x3f);
        }
        static const uint32_t least[4] = {0, 0x80, 0x800, 0x10000};
        if (c < least[k] || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return 0;
        if (c < 0x20 || c == 0x7f || (c >= 0x80 && c < 0xa0) || (c >= 0x202a && c <= 0x202e) ||
            (c >= 0x2066 && c <= 0x2069) || (c >= 0x200b && c <= 0x200f) || c == 0x2060 || c == 0xfeff)
            return 0;
        p += k + 1;
    }
    return 1;
}

static int rx_context(rx_ext *ext, const char *key, const char *value) {
    if (!rx_has(ext, "context-keys")) return rx_fail(ext, "unknown context-keys was not agreed");
    size_t kn = key ? strlen(key) : 0;
    if (!kn || kn > 40 || key[0] < 'a' || key[0] > 'z' ||
        strspn(key, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != kn)
        return rx_fail(ext, "syntax a context key is a lowercase letter, then letters, digits and _, at most 40 bytes");
    size_t vn = value ? strlen(value) : 0;
    if (vn > 256) return rx_fail(ext, "toobig a context value is at most 256 bytes");
    if (!vn || value[0] == ' ' || !rx_plain_text(value, vn)) return rx_fail(ext, "syntax a context value is one line of text");
    rx_buf line = {NULL, 0, 0};
    rx_buf_fmt(&line, "set-context %s %s", key, value);
    int r = rx_request(ext, line.data, NULL, 0, NULL, NULL);
    rx_buf_free(&line);
    return r;
}

int rx_set_context(rx_ext *ext, const char *key, const char *value) {
    if (value && strcmp(value, "-") == 0) return rx_fail(ext, "syntax \"-\" removes a key: use rx_remove_context");
    return rx_context(ext, key, value);
}

int rx_remove_context(rx_ext *ext, const char *key) { return rx_context(ext, key, "-"); }

static int rx_one_of(const char *s, const char *const *words) {
    for (; *words; words++)
        if (strcmp(s, *words) == 0) return 1;
    return 0;
}

int rx_status_item_encode(const rx_status_item *it, rx_buf *out) {
    static const char *const colors[] = {"fg", "muted", "accent", "error", "warning", "success", NULL};
    static const char *const backgrounds[] = {"error", "warning", NULL};
    const char *align = it->align && *it->align ? it->align : "right";
    const char *text = it->text ? it->text : "", *tip = it->tooltip ? it->tooltip : "";
    size_t tn = strlen(text), pn = strlen(tip), wn = it->when ? strlen(it->when) : 0;
    if (!rx_command_name_ok(it->name) || (strcmp(align, "left") != 0 && strcmp(align, "right") != 0) ||
        it->priority < -1000 || it->priority > 1000 || tn > 120 || !rx_plain_text(text, tn) || pn > 500 ||
        (it->color && *it->color && !rx_one_of(it->color, colors)) ||
        (it->background && *it->background && !rx_one_of(it->background, backgrounds)) ||
        wn > 512 || (wn && strpbrk(it->when, "\r\n")))
        return -1;
    rx_buf extra = {NULL, 0, 0};
    if (it->command && *it->command && rx_command_ref(&extra, it->command, it->args, it->args_len) < 0) {
        rx_buf_free(&extra);
        return -1;
    }
    if (it->color && *it->color) rx_buf_fmt(&extra, "color %s 0\n", it->color);
    if (it->background && *it->background) rx_buf_fmt(&extra, "background %s 0\n", it->background);
    if (wn) {
        rx_buf_fmt(&extra, "when %zu\n", wn);
        rx_buf_add(&extra, it->when, wn);
    }
    rx_buf_fmt(out, "%s %d %zu %zu %zu\n", align, it->priority, tn, pn, extra.len);
    rx_buf_add(out, text, tn);
    rx_buf_add(out, tip, pn);
    rx_buf_add(out, extra.data ? extra.data : "", extra.len);
    rx_buf_free(&extra);
    return 0;
}

int rx_set_status_item(rx_ext *ext, const rx_status_item *item) {
    if (!rx_has(ext, "status-items")) return rx_fail(ext, "unknown status-items was not agreed");
    rx_buf body = {NULL, 0, 0};
    if (rx_status_item_encode(item, &body) < 0) {
        rx_buf_free(&body);
        return rx_fail(ext, "syntax not a valid status item");
    }
    rx_buf line = {NULL, 0, 0};
    rx_buf_fmt(&line, "status-item %s %zu", item->name, body.len);
    int r = rx_request(ext, line.data, body.data, body.len, NULL, NULL);
    rx_buf_free(&line);
    rx_buf_free(&body);
    return r;
}

int rx_remove_status_item(rx_ext *ext, const char *name) {
    if (!rx_has(ext, "status-items")) return rx_fail(ext, "unknown status-items was not agreed");
    if (!rx_command_name_ok(name)) return rx_fail(ext, "syntax a status item's name is 1 to 40 of a-z 0-9 _");
    char line[64];
    snprintf(line, sizeof line, "status-remove %s", name);
    return rx_request(ext, line, NULL, 0, NULL, NULL);
}

int64_t rx_scm_register(rx_ext *ext, const char *root, const char *label) {
    if (!rx_has(ext, "scm")) {
        snprintf(ext->error, sizeof ext->error, "unknown scm was not negotiated");
        return -1;
    }
    rx_buf body = {NULL, 0, 0};
    rx_buf_fmt(&body, "%zu %zu\n%s%s", strlen(root), strlen(label), root, label);
    char line[64];
    snprintf(line, sizeof line, "scm-register %zu", body.len);
    int64_t h = rx_req_num(ext, line, body.data, body.len);
    rx_buf_free(&body);
    return h;
}

int rx_scm_update(rx_ext *ext, int64_t scm, const rx_buf *body) {
    char line[64];
    snprintf(line, sizeof line, "scm-update %lld %zu", (long long)scm, body->len);
    return rx_request(ext, line, body->data, body->len, NULL, NULL);
}

int rx_scm_input(rx_ext *ext, int64_t scm, rx_buf *out) {
    char line[64];
    snprintf(line, sizeof line, "scm-input %lld", (long long)scm);
    return rx_request(ext, line, NULL, 0, NULL, out);
}

int rx_scm_input_set(rx_ext *ext, int64_t scm, const char *text) {
    char line[64];
    snprintf(line, sizeof line, "scm-input-set %lld %zu", (long long)scm, strlen(text));
    return rx_request(ext, line, text, strlen(text), NULL, NULL);
}

void rx_scm_head(rx_buf *b, const char *status, const char *placeholder, int nactions, int ngroups) {
    if (!placeholder) placeholder = "";
    rx_buf_fmt(b, "%zu %zu %d %d\n", strlen(status), strlen(placeholder), nactions, ngroups);
    rx_buf_str(b, status);
    rx_buf_str(b, placeholder);
}

void rx_scm_action(rx_buf *b, const char *icon, const char *command, const char *args, size_t args_len, const char *label) {
    if (!label) label = "";
    rx_buf_fmt(b, "action %s %s %zu %zu\n", icon && *icon ? icon : "-", command, args ? args_len : 0, strlen(label));
    if (args) rx_buf_add(b, args, args_len);
    rx_buf_str(b, label);
}

void rx_scm_group(rx_buf *b, const char *id, const char *label, int nactions, int nres) {
    rx_buf_fmt(b, "group %s %zu %d %d\n", id, strlen(label), nactions, nres);
    rx_buf_str(b, label);
}

void rx_scm_res(rx_buf *b, char code, const char *color, const char *path, int nrec) {
    rx_buf_fmt(b, "res %c %s %zu %d\n", code, color && *color ? color : "-", strlen(path), nrec);
    rx_buf_str(b, path);
}

void rx_scm_click(rx_buf *b, const char *command, const char *args, size_t args_len) {
    rx_buf_fmt(b, "command %s %zu\n", command, args ? args_len : 0);
    if (args) rx_buf_add(b, args, args_len);
}

void rx_scm_tip(rx_buf *b, const char *tip) {
    rx_buf_fmt(b, "tip %zu\n", strlen(tip));
    rx_buf_str(b, tip);
}

/* ---- when clauses: rhun's grammar, limits, messages and offsets (v1.1/when-vectors.txt) ---- */

static const char *const rx_when_keys[] = {
    "editorFocus", "textInputFocus", "paletteFocus", "findFocus", "explorerFocus", "agentsFocus",
    "terminalFocus", "scmInputFocus", "settingsFocus", "findVisible", "completionVisible", "signatureVisible",
    "snippetActive", "activeTab", "activeTabPinned", "tabCount", "resourceLang", "resourceExt",
    "resourceFilename", "resourceDirname", "resourcePath", "resourceScheme", "resourceInProject", "editorReadonly",
    "editorDirty", "editorHasSelection", "editorHasMultipleSelections", "editorProviders", "vimMode", "sidebarVisible",
    "agentsVisible", "terminalVisible", "projectOpen", "projectPath", "gitRepo", "gitBranch",
    "gitChanges", "isLinux", "isMac", "isWindows", "isWayland", "targetKind",
    "explorerResourceIsFolder", "explorerResourceIsRoot", "tabKind", "viewFocus", "view", "viewItem",
    "debugState", "inDebugMode", "debugType", "gitTrusted", "scmProvider", "scmResourceGroup",
    "scmResourceStatus", "notebookFocus", "notebookCellEditing", "notebookCellType", "notebookKernelState", "remoteName",
    "remoteHost",
    NULL};
static const char *const rx_when_settings[] = {
    "ui.follow_system", "ui.theme", "ui.dark_theme", "ui.light_theme",
    "ui.scale", "ui.font_size", "ui.font", "ui.file_icons",
    "ui.sidebar", "ui.sidebar_width", "ui.agents_panel", "ui.agents_width",
    "ui.tooltips", "ui.auto_hide_scrollbars", "ui.decorations", "ui.scroll_sensitivity",
    "ui.fast_scroll_sensitivity", "editor.font_size", "editor.font", "editor.line_height",
    "editor.tab_width", "editor.insert_spaces", "editor.line_numbers", "editor.highlight_line",
    "editor.animate_disk_changes", "editor.match_brackets", "editor.indent_guides", "editor.word_wrap",
    "editor.whitespace", "editor.cursor_blink", "editor.smooth_caret", "editor.auto_pairs",
    "editor.multi_cursor_paste", "editor.multi_cursor_modifier", "editor.scroll_past_end", "editor.vim_mode",
    "editor.complete_replace", "editor.complete_commit_characters", "editor.lightbulb", "editor.format_on_type",
    "editor.format_on_paste", "editor.folding", "files.trim_trailing_whitespace", "files.final_newline",
    "files.format_on_save", "files.format_on_save_timeout", "files.code_actions_on_save", "files.code_actions_on_save_timeout",
    "files.restore_session", "files.restore_project", "files.exclude", "agents.claude",
    "agents.codex", "agents.grok", "agents.opencode", "terminal.shell",
    "terminal.font_size", "terminal.font", "terminal.line_height", "terminal.scrollback",
    "terminal.height", "git.enabled", "git.commit_ai", "git.commit_model",
    "git.ai_setup", "extensions.secrets", "updates.check", "updates.check_now",
    "updates.extensions", "updates.check_extensions", "debug.show_on_start", "debug.keys_in_terminal",
    "debug.console_lines", "remote.agent_binary", "testing.gutter",
    NULL};

typedef struct {
    const unsigned char *s;
    size_t n, p, ops, depth, at;
    const char *err;
} rx_wp;

static void rx_w_fail_at(rx_wp *w, size_t at, const char *m) {
    if (!w->err) w->err = m, w->at = at;
}
static void rx_w_fail(rx_wp *w, const char *m) { rx_w_fail_at(w, w->p, m); }
static void rx_w_ws(rx_wp *w) {
    while (w->p < w->n && (w->s[w->p] == ' ' || w->s[w->p] == '\t')) w->p++;
}
static unsigned char rx_w_at(rx_wp *w, size_t i) { return i < w->n ? w->s[i] : 0; }
static void rx_w_emit(rx_wp *w) {
    if (w->ops >= 64) rx_w_fail(w, "too many operations (64 at most)");
    else w->ops++;
}
static int rx_w_wordbyte(unsigned char c) {
    return c == '_' || c == '.' || c == '/' || c == '-' || (c >= '0' && c <= '9') || ((c | 0x20) >= 'a' && (c | 0x20) <= 'z');
}
static size_t rx_w_word(rx_wp *w) {
    size_t s = w->p;
    while (w->p < w->n && rx_w_wordbyte(w->s[w->p])) w->p++;
    return w->p - s;
}
static int rx_w_is(rx_wp *w, size_t s, size_t n, const char *word) {
    return n == strlen(word) && memcmp(w->s + s, word, n) == 0;
}
static size_t rx_w_bare(rx_wp *w) {
    size_t s = w->p;
    while (w->p < w->n) {
        unsigned char c = w->s[w->p];
        if (!(strchr("_-./:*?+#@$", c) && c) && !((c >= '0' && c <= '9') || ((c | 0x20) >= 'a' && (c | 0x20) <= 'z'))) break;
        w->p++;
    }
    return w->p - s;
}
static int rx_w_utf8(const unsigned char *s, size_t n) {
    for (size_t i = 0; i < n;) {
        unsigned c = s[i];
        size_t k = c < 0x80 ? 0 : (c & 0xe0) == 0xc0 ? 1 : (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 ? 3 : 9;
        if (k == 9 || i + k >= n) return 0;
        uint32_t v = k ? c & (0x3f >> k) : c;
        for (size_t j = 1; j <= k; j++) {
            if ((s[i + j] & 0xc0) != 0x80) return 0;
            v = v << 6 | (s[i + j] & 0x3f);
        }
        static const uint32_t least[4] = {0, 0x80, 0x800, 0x10000};
        if (v < least[k] || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff)) return 0;
        i += k + 1;
    }
    return 1;
}
static void rx_w_quoted(rx_wp *w) {
    unsigned char q = w->s[w->p];
    size_t open = w->p, i = w->p + 1;
    for (; i < w->n && w->s[i] != q; i++)
        if (w->s[i] < 0x20 || w->s[i] == 0x7f) { rx_w_fail_at(w, open, "unclosed quote"); return; }
    if (i >= w->n) { rx_w_fail_at(w, open, "unclosed quote"); return; }
    if (!rx_w_utf8(w->s + open + 1, i - open - 1)) { rx_w_fail_at(w, open, "a quoted value that is not UTF-8"); return; }
    w->p = i + 1;
}
static int rx_w_listed(const char *const *list, const unsigned char *s, size_t n) {
    for (; *list; list++)
        if (strlen(*list) == n && memcmp(*list, s, n) == 0) return 1;
    return 0;
}
static void rx_w_key(rx_wp *w, size_t start, size_t n) {
    const unsigned char *k = w->s + start;
    int ok = 0;
    if (n > 7 && memcmp(k, "config.", 7) == 0) {
        const unsigned char *r = k + 7;
        size_t rn = n - 7;
        const unsigned char *d = (const unsigned char *)memchr(r, '.', rn);
        ok = d && d > r && (size_t)(d - r) + 1 < rn && rx_w_listed(rx_when_settings, r, rn);
    } else {
        const unsigned char *d = (const unsigned char *)memchr(k, '.', n);
        if (!d) {
            ok = rx_w_listed(rx_when_keys, k, n);
        } else if (d > k && (size_t)(d - k) + 1 < n) {
            const unsigned char *r = d + 1;
            size_t rn = n - (size_t)(r - k);
            if (rn > 8 && memcmp(r, "setting.", 8) == 0) {
                r += 8, rn -= 8;
                ok = rn && rn <= 64;
                for (size_t i = 0; ok && i < rn; i++) ok = r[i] == '_' || (r[i] >= '0' && r[i] <= '9') || (r[i] >= 'a' && r[i] <= 'z');
            } else {
                ok = rn && rn <= 40 && r[0] >= 'a' && r[0] <= 'z';
                for (size_t i = 1; ok && i < rn; i++) ok = r[i] == '_' || (r[i] >= '0' && r[i] <= '9') || ((r[i] | 0x20) >= 'a' && (r[i] | 0x20) <= 'z');
            }
        }
    }
    if (ok) rx_w_emit(w);
    else rx_w_fail_at(w, start, "unknown key");
}
static void rx_w_or(rx_wp *w);
static int rx_w_in(rx_wp *w) {
    size_t s = w->p, n = rx_w_word(w);
    if (rx_w_is(w, s, n, "in")) return 1;
    if (rx_w_is(w, s, n, "not")) {
        rx_w_ws(w);
        size_t a = w->p, m = rx_w_word(w);
        if (rx_w_is(w, a, m, "in")) return 2;
        w->p = a;
        rx_w_fail(w, "expected in or not in");
        return 0;
    }
    w->p = s;
    return 0;
}
static void rx_w_list(rx_wp *w) {
    rx_w_ws(w);
    unsigned char a = rx_w_at(w, w->p);
    if (a == '\'' || a == '"') {
        rx_w_quoted(w);
        if (!w->err) rx_w_emit(w);
        return;
    }
    size_t s = w->p, n = rx_w_word(w);
    if (!n) { rx_w_fail(w, "expected a key or a quoted list"); return; }
    rx_w_key(w, s, n);
}
static void rx_w_value(rx_wp *w) {
    rx_w_ws(w);
    unsigned char a = rx_w_at(w, w->p);
    if (a == '\'' || a == '"') rx_w_quoted(w);
    else if (!rx_w_bare(w)) { rx_w_fail(w, "expected a value"); return; }
    if (!w->err) rx_w_emit(w);
}
static void rx_w_tail(rx_wp *w) {
    rx_w_ws(w);
    unsigned char a = rx_w_at(w, w->p), b = rx_w_at(w, w->p + 1);
    int two;
    if (a == '=') {
        if (b != '=' && b != '~') { rx_w_fail(w, "expected == != < <= > >= =~ in or not in"); return; }
        two = 1;
    } else if (a == '!') {
        if (b != '=') { rx_w_fail(w, "expected == != < <= > >= =~ in or not in"); return; }
        two = 1;
    } else if (a == '<' || a == '>') {
        two = b == '=';
    } else {
        if (rx_w_in(w)) {
            rx_w_list(w);
            rx_w_emit(w);
        }
        return;
    }
    w->p += two ? 2 : 1;
    rx_w_value(w);
    rx_w_emit(w);
}
static void rx_w_unary(rx_wp *w) {
    if (w->err) return;
    rx_w_ws(w);
    unsigned char a = rx_w_at(w, w->p), b = rx_w_at(w, w->p + 1);
    if (a == '!') {
        if (b == '=') { rx_w_fail(w, "expected a key, true, false, ! or ("); return; }
        w->p++;
        if (++w->depth > 8) { rx_w_fail(w, "nested too deeply (8 levels at most)"); return; }
        rx_w_unary(w);
        w->depth--;
        rx_w_emit(w);
    } else if (a == '(') {
        w->p++;
        if (++w->depth > 8) { rx_w_fail(w, "nested too deeply (8 levels at most)"); return; }
        rx_w_or(w);
        if (w->err) return;
        w->depth--;
        rx_w_ws(w);
        if (rx_w_at(w, w->p) != ')') { rx_w_fail(w, "expected )"); return; }
        w->p++;
    } else if (a == '\'' || a == '"') {
        rx_w_quoted(w);
        if (w->err) return;
        rx_w_emit(w);
        rx_w_ws(w);
        if (!rx_w_in(w)) { rx_w_fail(w, "expected in or not in"); return; }
        rx_w_list(w);
        rx_w_emit(w);
    } else {
        size_t s = w->p, n = rx_w_word(w);
        if (!n) { rx_w_fail(w, "expected a key, true, false, ! or ("); return; }
        if (rx_w_is(w, s, n, "true") || rx_w_is(w, s, n, "false")) { rx_w_emit(w); return; }
        rx_w_key(w, s, n);
        if (!w->err) rx_w_tail(w);
    }
}
static void rx_w_and(rx_wp *w) {
    rx_w_unary(w);
    while (!w->err) {
        rx_w_ws(w);
        if (rx_w_at(w, w->p) != '&' || rx_w_at(w, w->p + 1) != '&') return;
        w->p += 2;
        rx_w_unary(w);
        rx_w_emit(w);
    }
}
static void rx_w_or(rx_wp *w) {
    rx_w_and(w);
    while (!w->err) {
        rx_w_ws(w);
        if (rx_w_at(w, w->p) != '|' || rx_w_at(w, w->p + 1) != '|') return;
        w->p += 2;
        rx_w_and(w);
        rx_w_emit(w);
    }
}

int rx_when_check(const char *clause, size_t len, size_t *offset, const char **msg) {
    rx_wp w;
    memset(&w, 0, sizeof w);
    w.s = (const unsigned char *)clause;
    w.n = len;
    if (len > 512) {
        w.err = "too long (512 bytes at most)", w.at = 512;
    } else {
        rx_w_ws(&w);
        if (w.p >= w.n) {
            w.err = "empty clause", w.at = 0;
        } else {
            rx_w_or(&w);
            if (!w.err) {
                rx_w_ws(&w);
                if (w.p < w.n) rx_w_fail(&w, "expected && or ||");
            }
        }
    }
    if (!w.err) return 0;
    if (offset) *offset = w.at;
    if (msg) *msg = w.err;
    return -1;
}

int rx_cmd(rx_ext *ext, const char *name) {
    rx_buf line = {NULL, 0, 0};
    rx_buf_fmt(&line, "cmd %s", name);
    int r = rx_request(ext, line.data, NULL, 0, NULL, NULL);
    rx_buf_free(&line);
    return r;
}

int64_t rx_view(rx_ext *ext, const char *title) {
    rx_buf line = {NULL, 0, 0};
    rx_buf_fmt(&line, "view %s", title);
    int64_t v = rx_req_num(ext, line.data, NULL, 0);
    rx_buf_free(&line);
    return v;
}

int rx_draw(rx_ext *ext, int64_t view, const rx_buf *picture) {
    char line[96];
    size_t n = picture ? picture->len : 0;
    snprintf(line, sizeof line, "draw %lld %zu", (long long)view, n);
    return rx_request(ext, line, n ? picture->data : NULL, n, NULL, NULL);
}

int rx_unview(rx_ext *ext, int64_t view) {
    char line[64];
    snprintf(line, sizeof line, "unview %lld", (long long)view);
    return rx_request(ext, line, NULL, 0, NULL, NULL);
}

static int rx_wedit_finish_caps(rx_wedit *e, rx_buf *out, int ws, int ops, const char *fixed);
static int rx_positive_num(const char *s, int64_t max, int64_t *out);

/* the workspace edit grammar rhun agreed: 1 workspace-edits, 2 file-operations */
static int rx_edit_caps(rx_ext *ext) {
    return (rx_has(ext, "workspace-edits") ? 1 : 0) | (rx_has(ext, "file-operations") ? 2 : 0);
}

const rx_apply_error *rx_last_apply_error(rx_ext *ext) { return &ext->apply_error; }

/* keeps a refusal as rx_last_apply_error: "CODE TEXT", with the feature "CODE ENTRY TEXT" */
static void rx_keep_apply_error(rx_ext *ext, const char *reply, int entries) {
    rx_apply_error *a = &ext->apply_error;
    const char *sp = strchr(reply, ' ');
    size_t cl = sp ? (size_t)(sp - reply) : strlen(reply);
    if (cl >= sizeof a->code) cl = sizeof a->code - 1;
    memcpy(a->code, reply, cl);
    a->code[cl] = 0;
    const char *text = sp ? sp + 1 : "";
    a->entry = 0;
    if (entries) {
        int64_t n;
        const char *sp2 = strchr(text, ' ');
        char num[16];
        size_t nl = sp2 ? (size_t)(sp2 - text) : strlen(text);
        if (nl > 0 && nl < sizeof num) {
            memcpy(num, text, nl);
            num[nl] = 0;
            if (rx_positive_num(num, 2147483647, &n)) {
                a->entry = (int)n;
                text = sp2 ? sp2 + 1 : "";
            }
        }
    }
    snprintf(a->text, sizeof a->text, "%s", text);
}

int rx_apply(rx_ext *ext, rx_wedit *edit) {
    rx_buf body = {NULL, 0, 0};
    int caps = rx_edit_caps(ext);
    memset(&ext->apply_error, 0, sizeof ext->apply_error);
    if (rx_wedit_finish_caps(edit, &body, caps & 1, (caps & 2) != 0, NULL) < 0) {
        snprintf(ext->error, sizeof ext->error, "%s %s", edit->fail, edit->why);
        rx_keep_apply_error(ext, ext->error, 0);
        rx_wedit_free(edit);
        return -1;
    }
    char line[64];
    snprintf(line, sizeof line, "apply %zu", body.len);
    int r = rx_request(ext, line, body.data, body.len, NULL, NULL);
    rx_buf_free(&body);
    if (r < 0) rx_keep_apply_error(ext, ext->error, caps & 1);
    return r;
}

/* Decimal fields with overflow checks, including identities received from the host. */
static int rx_positive_num(const char *s, int64_t max, int64_t *out) {
    int64_t value = 0;
    if (!s || !*s) return 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9' || value > (max - (*s - '0')) / 10) return 0;
        value = value * 10 + (*s - '0');
    }
    *out = value;
    return 1;
}

int64_t rx_save_apply(rx_ext *ext, int64_t request_id, rx_doc doc, int64_t version, rx_wedit *edit) {
    rx_buf body = {NULL, 0, 0}, fields = {NULL, 0, 0};
    int64_t result = -1;
    const char *code = NULL, *why = NULL;
    if (!rx_has(ext, "save-actions")) { code = "unknown"; why = "save-actions feature was not agreed"; }
    else if (request_id <= 0 || request_id > 2147483647 || doc <= 0 || version < 0) {
        code = "syntax"; why = "invalid save-action identity";
    }
    if (code) {
        snprintf(ext->error, sizeof ext->error, "%s %s", code, why);
        rx_wedit_free(edit);
        return -1;
    }
    if (rx_wedit_finish_caps(edit, &body, 0, 0, "save-apply") < 0) {   /* 12g's grammar: one open document */
        snprintf(ext->error, sizeof ext->error, "%s %s", edit->fail, edit->why);
        rx_wedit_free(edit);
    } else {
        char line[128];
        snprintf(line, sizeof line, "save-apply %lld %lld %lld %zu", (long long)request_id, (long long)doc, (long long)version, body.len);
        if (rx_request(ext, line, body.data, body.len, &fields, NULL) == 0 &&
            (!rx_positive_num(fields.data, INT64_MAX, &result) || result < version)) {
            snprintf(ext->error, sizeof ext->error, "syntax invalid save-apply reply");
            result = -1;
        }
    }
    rx_buf_free(&body);
    rx_buf_free(&fields);
    return result;
}

static int rx_snippet_utf8(const unsigned char *s, size_t n) {
    size_t at = 0;
    while (at < n) {
        unsigned char c = s[at++];
        size_t more;
        if (c < 0x80) continue;
        if (c >= 0xc2 && c <= 0xdf) more = 1;
        else if (c >= 0xe0 && c <= 0xef) more = 2;
        else if (c >= 0xf0 && c <= 0xf4) more = 3;
        else return 0;
        if (more > n - at) return 0;
        if ((c == 0xe0 && s[at] < 0xa0) || (c == 0xed && s[at] >= 0xa0) ||
            (c == 0xf0 && s[at] < 0x90) || (c == 0xf4 && s[at] >= 0x90)) return 0;
        for (size_t j = 0; j < more; j++) if ((s[at++] & 0xc0) != 0x80) return 0;
    }
    return 1;
}

/* Portable compiled programs. SIZE_MAX denotes success for private readers. */
static uint32_t rx_st_word(const unsigned char *p, size_t at) {
    return (uint32_t)p[at] | ((uint32_t)p[at+1] << 8) |
        ((uint32_t)p[at+2] << 16) | ((uint32_t)p[at+3] << 24);
}
static int rx_st_scalar(uint32_t n) { return n <= 0x10ffff && !(n >= 0xd800 && n <= 0xdfff); }
static size_t rx_st_regex(const unsigned char *p, size_t len) {
    size_t code, classes, ranges, repeats, class_at, range_at, repeat_at, size, next;
    uint32_t captures, slots;
    if (len < 48) return len;
    if (len > 262144) return 40;
    if (!p || rx_st_word(p,0) != 0x58524e52) return 0;
    if (rx_st_word(p,4) != 1) return 4;
    if (rx_st_word(p,8) != 0x00100000) return 8;
    if (rx_st_word(p,44) != 0) return 44;
    code=rx_st_word(p,20); classes=rx_st_word(p,24); ranges=rx_st_word(p,28); repeats=rx_st_word(p,32);
    if (!code || code>8192) return 20;
    if (classes>256) return 24;
    if (ranges>8192) return 28;
    if (repeats>256) return 32;
    class_at=48+code*16; range_at=class_at+classes*8; repeat_at=range_at+ranges*8; size=repeat_at+repeats*16;
    if (size>262144 || size!=len || rx_st_word(p,40)!=size) return 40;
    if (rx_st_word(p,12)&~127u) return 12;
    captures=rx_st_word(p,16);
    if (captures>64) return 16;
    if (rx_st_word(p,36)>=code) return 36;
    slots=2*(captures+1);
    for (size_t i=0; i<code; i++) {
        size_t at=48+i*16;
        uint32_t op=rx_st_word(p,at), x=rx_st_word(p,at+4), y=rx_st_word(p,at+8), z=rx_st_word(p,at+12);
        int valid;
        switch (op) {
        case 0: case 2:
            if (x|y|z) return at+4;
            break;
        case 1: case 3: case 4: case 5: case 9: case 10:
            if (op==1) valid=rx_st_scalar(x);
            else if (op==3) valid=x<classes;
            else if (op==4) valid=x>=1 && x<=4;
            else if (op==5) valid=x>=2 && x<slots;
            else if (op==9) valid=x>=1 && x<=captures;
            else valid=x<repeats;
            if (!valid) return at+4;
            if (y|z) return at+8;
            break;
        case 6:
            if (x<2 || (x&1) || !y || (y&1) || (uint64_t)x+y>slots) return at+4;
            if (z) return at+12;
            break;
        case 14: case 15:
            if (op==14 ? (x<1 || x>4) : (x<1 || x>captures)) return at+4;
            if (y & ~(op==14 ? 6u : 2u)) return at+8;
            if (z) return at+12;
            break;
        case 7: case 8: case 11: case 12: case 13:
            if ((op==11 || op==12) ? x>=repeats : x>=code) return at+4;
            if (op==8) {
                if (y|z) return at+8;
            } else {
                if (y>=code) return at+8;
                if (op==11) { if (z>=code) return at+12; }
                else if ((op==13 && (z&~3u)) || (op!=13 && z)) return at+12;
            }
            break;
        default: return at;
        }
        if (((op>=1 && op<=6) || op==9 || op==10 || op==14 || op==15) && i+1==code) return at;
    }
    next=0;
    for (size_t i=0; i<classes; i++) {
        size_t at=class_at+i*8, count=rx_st_word(p,at+4);
        uint32_t previous=0;
        if (rx_st_word(p,at)!=next || count>ranges-next) return at;
        for (size_t j=0; j<count; j++) {
            size_t r=range_at+(next+j)*8;
            uint32_t lo=rx_st_word(p,r), hi=rx_st_word(p,r+4);
            if (!rx_st_scalar(lo) || !rx_st_scalar(hi) || lo>hi || (lo<0xd800 && hi>0xdfff)) return r;
            if (j && previous+1>=lo) return r;
            previous=hi;
        }
        next+=count;
    }
    if (next!=ranges) return range_at+next*8;
    for (size_t i=0; i<repeats; i++) {
        size_t at=repeat_at+i*16;
        uint32_t min=rx_st_word(p,at), max=rx_st_word(p,at+4);
        if (min>0x7fffffff) return at;
        if (max!=0xffffffff && (max>0x7fffffff || max<min)) return at+4;
        if (rx_st_word(p,at+8)&~1u) return at+8;
        if (rx_st_word(p,at+12)) return at+12;
    }
    return SIZE_MAX;
}
static size_t rx_st_utf8_error(const unsigned char *s, size_t n) {
    size_t at=0;
    while (at<n) {
        size_t start=at, more;
        unsigned char c=s[at++];
        if (c<0x80) continue;
        if (c>=0xc2 && c<=0xdf) more=1;
        else if (c>=0xe0 && c<=0xef) more=2;
        else if (c>=0xf0 && c<=0xf4) more=3;
        else return start;
        if (more>n-at) return start;
        if ((c==0xe0 && s[at]<0xa0) || (c==0xed && s[at]>=0xa0) ||
            (c==0xf0 && s[at]<0x90) || (c==0xf4 && s[at]>=0x90)) return start;
        for (size_t j=0; j<more; j++) if ((s[at++]&0xc0)!=0x80) return start;
    }
    return SIZE_MAX;
}
static size_t rx_st_span(const unsigned char *p, size_t at, int used,
                         const unsigned char *text, size_t n, size_t *cursor) {
    size_t offset=rx_st_word(p,at), len=rx_st_word(p,at+4);
    if (!used) return (offset|len) ? at : SIZE_MAX;
    if (offset!=*cursor) return at;
    if (len>n-*cursor) return at+4;
    if (*cursor!=n && (text[*cursor]&0xc0)==0x80) return at;
    if (*cursor+len!=n && (text[*cursor+len]&0xc0)==0x80) return at+4;
    *cursor+=len;
    return SIZE_MAX;
}
static size_t rx_st_replacement(const unsigned char *p, size_t len) {
    size_t count, blob, cursor=0, error;
    uint64_t size;
    if (len<32) return len;
    if (len>(4u<<20)) return 20;
    if (!p || rx_st_word(p,0)!=0x46524e52) return 0;
    if (rx_st_word(p,4)!=1) return 4;
    if (rx_st_word(p,8)!=0x00100000) return 8;
    if (rx_st_word(p,24)) return 24;
    if (rx_st_word(p,28)) return 28;
    count=rx_st_word(p,12);
    if (count>8192) return 12;
    blob=32+count*24;
    size=(uint64_t)blob+rx_st_word(p,16);
    if (size>(4u<<20) || size!=len || rx_st_word(p,20)!=size) return 20;
    error=rx_st_utf8_error(p+blob,len-blob);
    if (error!=SIZE_MAX) return blob+error;
    for (size_t i=0; i<count; i++) {
        size_t at=32+i*24;
        uint32_t op=rx_st_word(p,at), index=rx_st_word(p,at+4);
        if (op>11) return at;
        if (index>0x7fffffff || (!op && index)) return at+4;
        error=rx_st_span(p,at+8,!op || op>=9,p+blob,len-blob,&cursor);
        if (error!=SIZE_MAX) return error;
        error=rx_st_span(p,at+16,op==11,p+blob,len-blob,&cursor);
        if (error!=SIZE_MAX) return error;
    }
    return cursor!=len-blob ? blob+cursor : SIZE_MAX;
}
static size_t rx_st_payload(const unsigned char *p, size_t len) {
    size_t count, cursor;
    uint32_t previous=0;
    if (len<32) return len;
    if (len>(4u<<20)) return 16;
    if (!p || rx_st_word(p,0)!=0x58544e53) return 0;
    if (rx_st_word(p,4)!=1) return 4;
    if (rx_st_word(p,8)!=0x00100000) return 8;
    if (rx_st_word(p,20)) return 20;
    if (rx_st_word(p,24)) return 24;
    if (rx_st_word(p,28)) return 28;
    count=rx_st_word(p,12);
    if (count>512) return 12;
    cursor=32+count*24;
    if (rx_st_word(p,16)!=len || cursor>len) return 16;
    for (size_t i=0; i<count; i++) {
        size_t at=32+i*24, rlen, roff, flen, foff, error;
        uint32_t field=rx_st_word(p,at);
        if (field<=previous || field>512) return at;
        if (rx_st_word(p,at+20)) return at+20;
        if (rx_st_word(p,at+4)!=cursor) return at+4;
        rlen=rx_st_word(p,at+8);
        if (rlen>len-cursor) return at+8;
        roff=cursor; cursor+=rlen;
        if (rx_st_word(p,at+12)!=cursor) return at+12;
        flen=rx_st_word(p,at+16);
        if (flen>len-cursor) return at+16;
        foff=cursor; cursor+=flen;
        error=rx_st_regex(p+roff,rlen);
        if (error!=SIZE_MAX) return roff+error;
        error=rx_st_replacement(p+foff,flen);
        if (error!=SIZE_MAX) return foff+error;
        previous=field;
    }
    return cursor!=len ? 16 : SIZE_MAX;
}
int rx_snippet_transform_validate(const void *bytes, size_t len, size_t *error_offset) {
    size_t error=rx_st_payload((const unsigned char *)bytes,len);
    if (error_offset) *error_offset=error==SIZE_MAX ? 0 : error;
    return error==SIZE_MAX;
}

int rx_snippet_transform_program_validate(const rx_snippet_transform_program *p, int *part, size_t *error_offset) {
    size_t error=0;
    int which=0;
    if (p) {
        if (p->regex_len>262144) { which=1; error=40; }
        else if (p->replacement_len>(4u<<20)) { which=2; error=20; }
        else if (p->replacement_len>(4u<<20)-56-p->regex_len) { error=0; }
        else {
            error=rx_st_regex((const unsigned char *)p->regex,p->regex_len);
            if (error!=SIZE_MAX) which=1;
            else {
                error=rx_st_replacement((const unsigned char *)p->replacement,p->replacement_len);
                if (error!=SIZE_MAX) which=2;
            }
        }
    }
    if (part) *part=which;
    if (error_offset) *error_offset=error==SIZE_MAX ? 0 : error;
    return error==SIZE_MAX;
}

static int rx_snippet_ancestor(const rx_snippet *s, size_t parent, size_t child) {
    for (size_t p = s->fields[child].parent; p; p = s->fields[p - 1].parent)
        if (p - 1 == parent) return 1;
    return 0;
}

static size_t rx_snippet_extra_size(const rx_snippet *s) {
    size_t n = 0;
    for (size_t i = 0; i < s->nfields; i++) {
        rx_snippet_field f = s->fields[i];
        n += (size_t)snprintf(NULL, 0, "field %lld %zu %zu %zu 0\n", (long long)f.group, f.start, f.end, f.parent);
    }
    return n;
}

static int rx_snippet_validate_variants(const rx_snippet *s, const unsigned char variants[512]) {
    int64_t groups[256];
    size_t primary[256], depths[512], group_index[512], n = 0;
    unsigned char edges[256][256] = {{0}};
    size_t indegree[256] = {0}, queue[256], nq = 0;
    int final_seen = 0;
    if (!s || s->len > (4u << 20) || s->nfields > 512 ||
        (s->len && !s->text) || (s->nfields && !s->fields) ||
        !rx_snippet_utf8((const unsigned char *)s->text, s->len)) return 0;
    for (size_t i = 0; i < s->nfields; i++) {
        rx_snippet_field f = s->fields[i];
        if (f.group < 0 || f.group > 2147483647 || f.start > f.end || f.end > s->len || f.parent > i ||
            (f.start < s->len && ((unsigned char)s->text[f.start] & 0xc0) == 0x80) ||
            (f.end < s->len && ((unsigned char)s->text[f.end] & 0xc0) == 0x80)) return 0;
        depths[i] = 1;
        if (f.parent) {
            rx_snippet_field p = s->fields[f.parent - 1];
            if (p.group == 0 || primary[group_index[f.parent - 1]] != f.parent - 1 ||
                p.group == f.group || f.start < p.start || f.end > p.end) return 0;
            depths[i] = depths[f.parent - 1] + 1;
        }
        if (depths[i] > 32) return 0;
        if (!f.group) {
            if (final_seen || (variants && variants[i])) return 0;
            final_seen = 1;
            group_index[i] = 0;
            continue;
        }
        size_t g;
        for (g = 0; g < n && groups[g] != f.group; g++) {}
        if (g == n) {
            if (n == 256 || (variants && variants[i])) return 0;
            groups[n] = f.group;
            primary[n++] = i;
        } else {
            rx_snippet_field p = s->fields[primary[g]];
            int unequal = p.end - p.start != f.end - f.start ||
                (f.end > f.start && memcmp(s->text + p.start, s->text + f.start, f.end - f.start));
            if (!!unequal != !!(variants && variants[i])) return 0;
        }
        group_index[i] = g;
    }
    for (size_t i = 0; i < s->nfields; i++) {
        rx_snippet_field a = s->fields[i];
        for (size_t j = 0; j < i; j++) {
            rx_snippet_field b = s->fields[j];
            if (a.group && b.group && a.start < b.end && b.start < a.end &&
                (a.group == b.group || (!rx_snippet_ancestor(s, i, j) && !rx_snippet_ancestor(s, j, i)))) return 0;
        }
        if (!a.group) continue;
        size_t from = group_index[i];
        for (size_t p = a.parent; p; p = s->fields[p - 1].parent) {
            size_t to = group_index[p - 1];
            if (!edges[from][to]) { edges[from][to] = 1; indegree[to]++; }
        }
    }
    for (size_t i = 0; i < n; i++) if (!indegree[i]) queue[nq++] = i;
    for (size_t i = 0; i < nq; i++) {
        for (size_t to = 0; to < n; to++) {
            if (edges[queue[i]][to] && --indegree[to] == 0) queue[nq++] = to;
        }
    }
    if (nq != n) return 0;
    size_t extra = rx_snippet_extra_size(s);
    size_t header = (size_t)snprintf(NULL, 0, "0 0 0 0 snippet 0 %zu 0 %zu\n", s->len, extra);
    return header + s->len + extra <= (4u << 20);
}

int rx_snippet_validate(const rx_snippet *s) { return rx_snippet_validate_variants(s, NULL); }

static void rx_snippet_extra(const rx_snippet *s, rx_buf *out) {
    for (size_t i = 0; i < s->nfields; i++) {
        rx_snippet_field f = s->fields[i];
        rx_buf_fmt(out, "field %lld %zu %zu %zu 0\n", (long long)f.group, f.start, f.end, f.parent);
    }
}

static size_t rx_snippet_choice_size(const rx_snippet_choice *c) {
    size_t n = 0;
    for (size_t i = 0; i < c->noptions; i++)
        n += c->options[i].len + (size_t)snprintf(NULL, 0, "%zu\n", c->options[i].len);
    return n;
}

/* Length and identity preflight before any program bytes are inspected. */
static int rx_snippet_transforms_size(const rx_snippet_program *p, size_t *raw, size_t order[512]) {
    *raw=0;
    if (p->ntransforms>512 || (p->ntransforms && !p->transforms)) return 0;
    for (size_t i=0;i<512;i++) order[i]=SIZE_MAX;
    if (!p->ntransforms) return 1;
    size_t n=32+24*p->ntransforms;
    for (size_t i=0;i<p->ntransforms;i++) {
        const rx_snippet_field_transform *t=&p->transforms[i];
        if (!t->field || t->field>512 || t->field>p->snippet->nfields ||
            p->snippet->fields[t->field-1].group<=0 || order[t->field-1]!=SIZE_MAX) return 0;
        order[t->field-1]=i;
        if (t->transform.regex_len>262144 || t->transform.replacement_len>(4u<<20) ||
            t->transform.regex_len>(4u<<20)-n) return 0;
        n+=t->transform.regex_len;
        if (t->transform.replacement_len>(4u<<20)-n) return 0;
        n+=t->transform.replacement_len;
    }
    *raw=n;
    return 1;
}
static size_t rx_snippet_transforms_extra_size(size_t raw) {
    if (!raw) return 0;
    size_t encoded=4*((raw+2)/3);
    return encoded+(size_t)snprintf(NULL,0,"transforms 0 %zu\n",encoded);
}
static void rx_st_put(unsigned char *p, size_t at, size_t value) {
    for (size_t i=0;i<4;i++) p[at+i]=(unsigned char)(value>>(8*i));
}
/* Called only after validation and whole-body size checks. */
static void rx_snippet_transforms_extra(const rx_snippet_program *p, rx_buf *out) {
    if (!p->ntransforms) return;
    size_t raw,order[512];
    if (!rx_snippet_transforms_size(p,&raw,order)) abort();
    unsigned char *bytes=(unsigned char *)calloc(raw,1);
    if (!bytes) abort();
    rx_st_put(bytes,0,0x58544e53);rx_st_put(bytes,4,1);rx_st_put(bytes,8,0x00100000);
    rx_st_put(bytes,12,p->ntransforms);rx_st_put(bytes,16,raw);
    size_t row=32,cursor=32+24*p->ntransforms;
    for (size_t i=0;i<512;i++) {
        if (order[i]==SIZE_MAX) continue;
        const rx_snippet_transform_program *t=&p->transforms[order[i]].transform;
        rx_st_put(bytes,row,i+1);rx_st_put(bytes,row+4,cursor);rx_st_put(bytes,row+8,t->regex_len);
        memcpy(bytes+cursor,t->regex,t->regex_len);cursor+=t->regex_len;
        rx_st_put(bytes,row+12,cursor);rx_st_put(bytes,row+16,t->replacement_len);
        memcpy(bytes+cursor,t->replacement,t->replacement_len);cursor+=t->replacement_len;row+=24;
    }
    static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    rx_buf_fmt(out,"transforms 0 %zu\n",4*((raw+2)/3));
    for (size_t i=0;i<raw;i+=3) {
        unsigned int a=bytes[i],b=i+1<raw?bytes[i+1]:0,c=i+2<raw?bytes[i+2]:0;
        char quartet[4]={alphabet[a>>2],alphabet[((a&3)<<4)|(b>>4)],
            i+1<raw?alphabet[((b&15)<<2)|(c>>6)]:'=',i+2<raw?alphabet[c&63]:'='};
        rx_buf_add(out,quartet,4);
    }
    free(bytes);
}

/* Only called after validation; each intermediate sum is bounded by the frame cap. */
static size_t rx_snippet_program_extra_size(const rx_snippet_program *p) {
    size_t n = rx_snippet_extra_size(p->snippet);
    for (size_t i=0; i<p->ninitial_variants; i++)
        n += (size_t)snprintf(NULL, 0, "variant %zu 0\n", p->initial_variants[i]);
    for (size_t i = 0; i < p->nchoices; i++) {
        const rx_snippet_choice *c = &p->choices[i];
        size_t size = rx_snippet_choice_size(c);
        n += size + (size_t)snprintf(NULL, 0, "choice %lld %zu %zu\n", (long long)c->group, c->noptions, size);
    }
    size_t raw,order[512];
    if (!rx_snippet_transforms_size(p,&raw,order)) abort();
    return n+rx_snippet_transforms_extra_size(raw);
}

int rx_snippet_program_validate(const rx_snippet_program *p) {
    if (!p || !p->snippet || p->ninitial_variants > 512 ||
        (p->ninitial_variants && !p->initial_variants) || p->nchoices > 256 || (p->nchoices && !p->choices)) return 0;
    unsigned char variants[512] = {0};
    for (size_t i=0; i<p->ninitial_variants; i++) {
        size_t index = p->initial_variants[i];
        if (!index || index > 512 || index > p->snippet->nfields || variants[index-1]) return 0;
        variants[index-1] = 1;
    }
    if (!rx_snippet_validate_variants(p->snippet, variants)) return 0;
    const rx_snippet *s = p->snippet;
    size_t raw,order[512];
    if (!rx_snippet_transforms_size(p,&raw,order)) return 0;
    size_t total = rx_snippet_extra_size(s);
    for (size_t i=0; i<p->ninitial_variants; i++)
        total += (size_t)snprintf(NULL, 0, "variant %zu 0\n", p->initial_variants[i]);
    for (size_t i = 0; i < p->nchoices; i++) {
        const rx_snippet_choice *c = &p->choices[i];
        if (c->group <= 0 || !c->noptions || c->noptions > 256 || !c->options) return 0;
        for (size_t j = 0; j < i; j++) if (p->choices[j].group == c->group) return 0;
        size_t primary;
        for (primary = 0; primary < s->nfields && s->fields[primary].group != c->group; primary++) {}
        if (primary == s->nfields) return 0;
        rx_snippet_field f = s->fields[primary];
        if (c->options[0].len != f.end - f.start) return 0;
        size_t size = 0;
        for (size_t j = 0; j < c->noptions; j++) {
            rx_snippet_option o = c->options[j];
            if (o.len > (1u << 20) || (o.len && !o.text) || !rx_snippet_utf8((const unsigned char *)o.text, o.len)) return 0;
            size += o.len + (size_t)snprintf(NULL, 0, "%zu\n", o.len);
            if (size > (4u << 20)) return 0;
        }
        if (f.end > f.start && memcmp(c->options[0].text, s->text + f.start, f.end - f.start)) return 0;
        total += size + (size_t)snprintf(NULL, 0, "choice %lld %zu %zu\n", (long long)c->group, c->noptions, size);
        if (total > (4u << 20)) return 0;
    }
    total+=rx_snippet_transforms_extra_size(raw);
    size_t header = (size_t)snprintf(NULL, 0, "0 0 0 0 snippet 0 %zu 0 %zu\n", s->len, total);
    if (header+s->len+total>(4u<<20)) return 0;
    for (size_t i=0;i<p->ntransforms;i++)
        if (!rx_snippet_transform_program_validate(&p->transforms[i].transform,NULL,NULL)) return 0;
    return 1;
}

static void rx_snippet_program_extra(const rx_snippet_program *p, rx_buf *out) {
    rx_snippet_extra(p->snippet, out);
    for (size_t i=0; i<p->ninitial_variants; i++)
        rx_buf_fmt(out, "variant %zu 0\n", p->initial_variants[i]);
    for (size_t i = 0; i < p->nchoices; i++) {
        const rx_snippet_choice *c = &p->choices[i];
        rx_buf_fmt(out, "choice %lld %zu %zu\n", (long long)c->group, c->noptions, rx_snippet_choice_size(c));
        for (size_t j = 0; j < c->noptions; j++) {
            rx_buf_fmt(out, "%zu\n", c->options[j].len);
            rx_buf_add(out, c->options[j].text, c->options[j].len);
        }
    }
    rx_snippet_transforms_extra(p,out);
}

int rx_snippet_program_encode(const rx_snippet_program *p, rx_buf *out) {
    if (!out || !rx_snippet_program_validate(p)) return -1;
    rx_buf_fmt(out, "0 0 0 0 snippet 0 %zu 0 %zu\n", p->snippet->len, rx_snippet_program_extra_size(p));
    rx_buf_add(out, p->snippet->text, p->snippet->len);
    rx_snippet_program_extra(p, out);
    return 0;
}

int rx_snippet_encode(const rx_snippet *s, rx_buf *out) {
    rx_snippet_program p = {s, NULL, 0, NULL, 0, NULL, 0};
    return rx_snippet_program_encode(&p, out);
}

int64_t rx_insert_snippet(rx_ext *ext, rx_doc doc, int64_t version, rx_range r, const rx_snippet *s) {
    rx_snippet_program p = {s, NULL, 0, NULL, 0, NULL, 0};
    return rx_insert_snippet_program(ext, doc, version, r, &p);
}

int64_t rx_insert_snippet_program(rx_ext *ext, rx_doc doc, int64_t version, rx_range r, const rx_snippet_program *p) {
    rx_buf body = {NULL, 0, 0}, fields = {NULL, 0, 0};
    int64_t result = -1;
    if (!rx_has(ext, "snippets") || (p && p->ninitial_variants && !rx_has(ext, "snippet-variants")) ||
        (p && p->ntransforms && !rx_has(ext,"snippet-transforms"))) {
        snprintf(ext->error, sizeof ext->error, "unknown snippets feature was not agreed");
        return -1;
    }
    if (doc <= 0 || version < 0 || r.start.line <= 0 || r.start.col <= 0 || r.end.line <= 0 || r.end.col <= 0 ||
        r.end.line < r.start.line || (r.end.line == r.start.line && r.end.col < r.start.col) ||
        rx_snippet_program_encode(p, &body) < 0) {
        snprintf(ext->error, sizeof ext->error, "syntax invalid snippet target, range or fields");
        return -1;
    }
    char line[192];
    snprintf(line, sizeof line, "snippet %lld %lld %d %d %d %d %zu", (long long)doc, (long long)version,
             r.start.line, r.start.col, r.end.line, r.end.col, body.len);
    if (rx_request(ext, line, body.data, body.len, &fields, NULL) == 0 &&
        (!rx_positive_num(fields.data, INT64_MAX, &result) || result < version)) {
        snprintf(ext->error, sizeof ext->error, "syntax invalid snippet reply");
        result = -1;
    }
    rx_buf_free(&body);
    rx_buf_free(&fields);
    return result;
}

void rx_snippet_snapshot_free(rx_snippet_snapshot *s) {
    if (!s) return;
    rx_buf_free(&s->selected_text); rx_buf_free(&s->current_line); rx_buf_free(&s->path);
    rx_buf_free(&s->workspace); rx_buf_free(&s->indent); rx_buf_free(&s->line_comment);
    memset(s, 0, sizeof *s);
}

static int rx_snapshot_numbers(char *s, int64_t *out, size_t count) {
    for (size_t i=0; i<count; i++) {
        char *next = strchr(s, ' ');
        if ((i+1<count) != (next!=NULL)) return 0;
        if (next) *next = 0;
        if (!rx_positive_num(s, INT64_MAX, &out[i])) return 0;
        if (next) s = next+1;
    }
    return 1;
}

static int rx_snapshot_decode(const rx_buf *fields, const rx_buf *body, int64_t version, rx_pos at,
                              rx_snippet_snapshot *out) {
    char reply[128], head[257];
    int64_t r[2], n[11];
    size_t end=0, off, lengths[6];
    const char *parts[6];
    memset(out, 0, sizeof *out);
    if (!fields->data || fields->len>=sizeof reply || body->len>(4u<<20)) return 0;
    memcpy(reply, fields->data, fields->len); reply[fields->len]=0;
    if (!rx_snapshot_numbers(reply,r,2) || r[0]!=version || r[1]!=(int64_t)body->len) return 0;
    while (end<body->len && end<=256 && body->data[end]!='\n') end++;
    if (end>=body->len || end>256 || memchr(body->data,0,end)) return 0;
    memcpy(head,body->data,end); head[end]=0;
    if (strncmp(head,"context 1 ",10) || !rx_snapshot_numbers(head+10,n,11) || at.line<=0 || at.col<=0 ||
        n[0]!=at.line || n[1]!=at.col || n[2]<1 || n[2]>16 || n[3]>1 || n[4]>1) return 0;
    off=end+1;
    for (size_t i=0; i<6; i++) {
        if ((uint64_t)n[i+5]>body->len-off) return 0;
        lengths[i]=(size_t)n[i+5]; parts[i]=body->data+off;
        if (!rx_snippet_utf8((const unsigned char *)parts[i],lengths[i])) return 0;
        off+=lengths[i];
    }
    size_t col=(size_t)at.col-1, indent=0;
    if (off!=body->len || memchr(parts[1],'\n',lengths[1]) || col>lengths[1] ||
        (col<lengths[1] && ((unsigned char)parts[1][col]&0xc0)==0x80)) return 0;
    while (indent<col && (parts[1][indent]==' ' || parts[1][indent]=='\t')) indent++;
    if (lengths[4]!=indent || (indent && memcmp(parts[4],parts[1],indent))) return 0;
    out->version=version; out->position=at; out->tab_width=(int)n[2];
    out->insert_spaces=(int)n[3]; out->crlf=(int)n[4];
    rx_buf *buffers[6]={&out->selected_text,&out->current_line,&out->path,&out->workspace,&out->indent,&out->line_comment};
    for (size_t i=0; i<6; i++) rx_buf_add(buffers[i],parts[i],lengths[i]);
    return 1;
}

int rx_get_snippet_snapshot(rx_ext *ext, rx_doc doc, int64_t version, rx_range r, rx_snippet_snapshot *out) {
    if (!out) { snprintf(ext->error,sizeof ext->error,"syntax missing snippet context output"); return -1; }
    memset(out,0,sizeof *out);
    if (!rx_has(ext,"snippets")) {
        snprintf(ext->error,sizeof ext->error,"unknown snippets feature was not agreed"); return -1;
    }
    if (doc<=0 || version<0 || r.start.line<=0 || r.start.col<=0 || r.end.line<=0 || r.end.col<=0 ||
        r.end.line<r.start.line || (r.end.line==r.start.line && r.end.col<r.start.col)) {
        snprintf(ext->error,sizeof ext->error,"syntax invalid snippet target or range"); return -1;
    }
    char line[192]; rx_buf fields={NULL,0,0}, body={NULL,0,0};
    snprintf(line,sizeof line,"snippet-context %lld %lld %d %d %d %d",(long long)doc,(long long)version,
        r.start.line,r.start.col,r.end.line,r.end.col);
    int result=rx_request(ext,line,NULL,0,&fields,&body);
    if (result==0 && !rx_snapshot_decode(&fields,&body,version,r.start,out)) {
        snprintf(ext->error,sizeof ext->error,"syntax invalid snippet context reply"); result=-1;
    }
    rx_buf_free(&fields); rx_buf_free(&body);
    return result;
}

static int rx_source_kind(const char *s) {
    int segment = 0;
    if (!s || strlen(s) > 128) return 0;
    if (strcmp(s, "source") == 0) return 1;
    if (strncmp(s, "source.", 7) != 0) return 0;
    for (s += 7; *s; s++) {
        if (*s == '.') { if (!segment) return 0; segment = 0; }
        else if ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
                 (*s >= '0' && *s <= '9') || *s == '_' || *s == '-') segment = 1;
        else return 0;
    }
    return segment;
}

/* ---- pictures */

/* a number as the protocol writes it, at most two decimals, whatever the C locale says */
static void rx_num(rx_buf *b, double v) {
    if (!(v == v)) v = 0;                       /* NaN */
    if (v > 9999999) v = 9999999;
    if (v < -9999999) v = -9999999;
    long long cents = (long long)(v * 100 + (v < 0 ? -0.5 : 0.5));
    char s[48];
    int neg = cents < 0;
    if (neg) cents = -cents;
    long long whole = cents / 100, frac = cents % 100;
    if (frac == 0) snprintf(s, sizeof s, "%s%lld", neg && whole ? "-" : "", whole);
    else if (frac % 10 == 0) snprintf(s, sizeof s, "%s%lld.%lld", neg ? "-" : "", whole, frac / 10);
    else snprintf(s, sizeof s, "%s%lld.%02lld", neg ? "-" : "", whole, frac);
    rx_buf_add(b, " ", 1);
    rx_buf_str(b, s);
}

/* a decimal number as rhun writes it, whatever the C locale says */
static double rx_atof(const char *s) {
    double v = 0, scale = 1;
    int neg = *s == '-';
    if (neg) s++;
    for (; *s >= '0' && *s <= '9'; s++) v = v * 10 + (*s - '0');
    if (*s == '.')
        for (s++; *s >= '0' && *s <= '9'; s++) scale /= 10, v += (*s - '0') * scale;
    return neg ? -v : v;
}

static double rx_pos0(double v) { return v < 0 ? 0 : v; }

static void rx_color(rx_buf *p, const char *color) {
    rx_buf_add(p, " ", 1);
    rx_buf_str(p, color && *color ? color : "fg");
}

void rx_rect(rx_buf *p, double x, double y, double w, double h, const char *color) {
    rx_buf_str(p, "rect");
    rx_num(p, x), rx_num(p, y), rx_num(p, rx_pos0(w)), rx_num(p, rx_pos0(h));
    rx_color(p, color);
    rx_buf_add(p, "\n", 1);
}

void rx_rrect(rx_buf *p, double x, double y, double w, double h, double r, const char *color) {
    rx_buf_str(p, "rrect");
    rx_num(p, x), rx_num(p, y), rx_num(p, rx_pos0(w)), rx_num(p, rx_pos0(h)), rx_num(p, rx_pos0(r));
    rx_color(p, color);
    rx_buf_add(p, "\n", 1);
}

void rx_line(rx_buf *p, double x1, double y1, double x2, double y2, double width, const char *color) {
    rx_buf_str(p, "line");
    rx_num(p, x1), rx_num(p, y1), rx_num(p, x2), rx_num(p, y2), rx_num(p, rx_pos0(width));
    rx_color(p, color);
    rx_buf_add(p, "\n", 1);
}

static void rx_points(rx_buf *p, const double *xy, int points) {
    if (points > 10000) points = 10000;
    for (int i = 0; i < points; i++) rx_num(p, xy[2 * i]), rx_num(p, xy[2 * i + 1]);
}

void rx_poly(rx_buf *p, double width, const char *color, const double *xy, int points) {
    if (points < 2) return;
    rx_buf_str(p, "poly");
    rx_num(p, rx_pos0(width));
    rx_color(p, color);
    rx_points(p, xy, points);
    rx_buf_add(p, "\n", 1);
}

void rx_fill(rx_buf *p, const char *color, const double *xy, int points) {
    if (points < 3) return;
    rx_buf_str(p, "fill");
    rx_color(p, color);
    rx_points(p, xy, points);
    rx_buf_add(p, "\n", 1);
}

void rx_circle(rx_buf *p, double x, double y, double r, const char *color) {
    rx_buf_str(p, "circle");
    rx_num(p, x), rx_num(p, y), rx_num(p, rx_pos0(r));
    rx_color(p, color);
    rx_buf_add(p, "\n", 1);
}

void rx_ring(rx_buf *p, double x, double y, double r, double width, const char *color) {
    rx_buf_str(p, "ring");
    rx_num(p, x), rx_num(p, y), rx_num(p, rx_pos0(r)), rx_num(p, rx_pos0(width));
    rx_color(p, color);
    rx_buf_add(p, "\n", 1);
}

void rx_pie(rx_buf *p, double x, double y, double r, double from, double to, const char *color) {
    rx_buf_str(p, "pie");
    rx_num(p, x), rx_num(p, y), rx_num(p, rx_pos0(r)), rx_num(p, from), rx_num(p, to);
    rx_color(p, color);
    rx_buf_add(p, "\n", 1);
}

void rx_label(rx_buf *p, double x, double y, const char *size, const char *align, const char *color, const char *text) {
    rx_buf_str(p, "text");
    rx_num(p, x), rx_num(p, y);
    rx_buf_fmt(p, " %s %s %s ", size, align, color && *color ? color : "fg");
    for (const char *t = text; *t; t++) {
        char c = (*t == '\n' || *t == '\r') ? ' ' : *t;
        rx_buf_add(p, &c, 1);
    }
    rx_buf_add(p, "\n", 1);
}

void rx_clip(rx_buf *p, double x, double y, double w, double h) {
    rx_buf_str(p, "clip");
    rx_num(p, x), rx_num(p, y), rx_num(p, rx_pos0(w)), rx_num(p, rx_pos0(h));
    rx_buf_add(p, "\n", 1);
}

void rx_unclip(rx_buf *p) { rx_buf_str(p, "unclip\n"); }

/* ---- completion and signature help */

static size_t rx_one_line_len(const char *s) {
    size_t n = 0;
    while (s[n] && s[n] != '\n' && s[n] != '\r') n++;
    return n;
}

void rx_list_item_edits(rx_list *l, rx_range r, const char *kind, const char *label, const char *insert,
                        const char *detail, const char *doc, const rx_range *edits, const char *const *texts, int nedits) {
    rx_list_item_cursor(l, r, kind, label, insert, detail, doc, edits, texts, nedits, -1);
}

void rx_list_item_cursor(rx_list *l, rx_range r, const char *kind, const char *label, const char *insert,
                         const char *detail, const char *doc, const rx_range *edits, const char *const *texts,
                         int nedits, long cursor) {
    rx_list_item_extra(l, r, kind, label, insert, detail, doc, edits, texts, nedits, cursor, NULL);
}

static void rx_add_line(rx_buf *b, const char *s, size_t n);

/* the complete-items records of an item whose range is r: only those that are valid */
static void rx_item_records(const rx_item_extra *x, rx_range r, rx_buf *out) {
    if (x->resolve && x->resolve_len > 0 && x->resolve_len <= (16u << 10)) {
        rx_buf_fmt(out, "resolve %d %zu\n", x->resolve_edits ? 1 : 0, x->resolve_len);
        rx_buf_add(out, x->resolve, x->resolve_len);
    }
    if (x->replace && x->replace->start.line == r.start.line && x->replace->start.col == r.start.col)
        rx_buf_fmt(out, "replace %d %d %d %d 0\n", x->replace->start.line, x->replace->start.col,
                   x->replace->end.line, x->replace->end.col);
    char commit[17];
    size_t cn = 0;
    for (const char *c = x->commit; c && *c && cn < 16; c++)
        if (*c >= 0x21 && *c <= 0x7e && !memchr(commit, *c, cn)) commit[cn++] = *c;
    if (cn) {
        rx_buf_fmt(out, "commit %zu\n", cn);
        rx_buf_add(out, commit, cn);
    }
    const char *texts[2] = {x->filter, x->suffix}, *words[2] = {"filter", "suffix"};
    for (int i = 0; i < 2; i++) {
        if (!texts[i] || !*texts[i]) continue;
        size_t n = strlen(texts[i]);
        rx_buf_fmt(out, "%s %zu\n", words[i], n);
        rx_add_line(out, texts[i], n);
    }
    if (x->preselect) rx_buf_str(out, "preselect 0\n");
    if (x->deprecated) rx_buf_str(out, "deprecated 0\n");
    if (x->adjust) rx_buf_str(out, "adjust 0\n");
    if (x->after_signature) rx_buf_str(out, "after signature 0\n");
    if (x->after_complete) rx_buf_str(out, "after complete 0\n");
}

void rx_list_item_extra(rx_list *l, rx_range r, const char *kind, const char *label, const char *insert,
                        const char *detail, const char *doc, const rx_range *edits, const char *const *texts,
                        int nedits, long cursor, const rx_item_extra *more) {
    rx_buf extra = {NULL, 0, 0};
    for (int i = 0; i < nedits; i++) {
        size_t n = strlen(texts[i]);
        rx_buf_fmt(&extra, "edit %d %d %d %d %zu\n", edits[i].start.line, edits[i].start.col,
                   edits[i].end.line, edits[i].end.col, n);
        rx_buf_add(&extra, texts[i], n);
    }
    if (doc && *doc) {
        size_t n = strlen(doc);
        rx_buf_fmt(&extra, "doc %zu\n", n);
        rx_buf_add(&extra, doc, n);
    }
    if (!insert) insert = label;
    if (!detail) detail = "";
    size_t ll = rx_one_line_len(label), il = strlen(insert), dl = rx_one_line_len(detail);
    if (cursor >= 0 && (size_t)cursor <= il && ((size_t)cursor == il || ((unsigned char)insert[cursor] & 0xc0) != 0x80))
        rx_buf_fmt(&extra, "cursor %ld 0\n", cursor);
    if (more) rx_item_records(more, r, &extra);
    size_t size = 64 + ll + il + dl + extra.len;
    if (l->n >= 4096 || l->items.len + size > (1u << 20) - 64) {
        l->more = 1;                    /* past the caps: typing asks again */
        rx_buf_free(&extra);
        return;
    }
    rx_buf_fmt(&l->items, "%d %d %d %d %s %zu %zu %zu %zu\n", r.start.line, r.start.col, r.end.line, r.end.col,
               kind && *kind ? kind : "text", ll, il, dl, extra.len);
    rx_buf_add(&l->items, label, ll);
    rx_buf_add(&l->items, insert, il);
    rx_buf_add(&l->items, detail, dl);
    rx_buf_add(&l->items, extra.data, extra.len);
    l->n++;
    rx_buf_free(&extra);
}

void rx_list_item_snippet(rx_list *l, rx_range r, const char *kind, const char *label,
                         const rx_snippet *s, const char *detail, const char *doc,
                         const rx_range *edits, const char *const *texts, int nedits) {
    rx_snippet_program program = {s, NULL, 0, NULL, 0, NULL, 0};
    rx_list_item_snippet_program(l, r, kind, label, &program, detail, doc, edits, texts, nedits);
}

void rx_list_item_snippet_program(rx_list *l, rx_range r, const char *kind, const char *label,
                         const rx_snippet_program *p, const char *detail, const char *doc,
                         const rx_range *edits, const char *const *texts, int nedits) {
    rx_buf extra = {NULL, 0, 0};
    const rx_snippet *s = p ? p->snippet : NULL;
    if (!s || s->len > (1u << 20) || (s->len && !s->text)) { l->more = 1; return; }
    for (int i = 0; i < nedits; i++) {
        size_t n = strlen(texts[i]);
        rx_buf_fmt(&extra, "edit %d %d %d %d %zu\n", edits[i].start.line, edits[i].start.col,
                   edits[i].end.line, edits[i].end.col, n);
        rx_buf_add(&extra, texts[i], n);
    }
    if (doc && *doc) {
        rx_buf_fmt(&extra, "doc %zu\n", strlen(doc));
        rx_buf_str(&extra, doc);
    }
    if (rx_snippet_program_validate(p)) rx_snippet_program_extra(p, &extra);
    if (!label) label = "";
    if (!detail) detail = "";
    size_t ll = rx_one_line_len(label), dl = rx_one_line_len(detail);
    if (l->n >= 4096 || ll > (1u << 20) || dl > (1u << 20) || extra.len > (1u << 20) ||
        l->items.len + 64 + ll + s->len + dl + extra.len > (1u << 20) - 64) {
        l->more = 1;
        rx_buf_free(&extra);
        return;
    }
    rx_buf_fmt(&l->items, "%d %d %d %d %s %zu %zu %zu %zu\n", r.start.line, r.start.col, r.end.line, r.end.col,
               kind && *kind ? kind : "text", ll, s->len, dl, extra.len);
    rx_buf_add(&l->items, label, ll);
    rx_buf_add(&l->items, s->text, s->len);
    rx_buf_add(&l->items, detail, dl);
    rx_buf_add(&l->items, extra.data, extra.len);
    l->n++;
    rx_buf_free(&extra);
}

void rx_list_item(rx_list *l, rx_range r, const char *kind, const char *label, const char *insert,
                  const char *detail, const char *doc) {
    rx_list_item_edits(l, r, kind, label, insert, detail, doc, NULL, NULL, 0);
}

void rx_list_finish(rx_list *l, rx_buf *out) {
    rx_buf_fmt(out, "%d %d\n", l->n, l->more ? 1 : 0);
    rx_buf_add(out, l->items.data, l->items.len);
    rx_buf_free(&l->items);
    l->n = l->more = 0;
}

void rx_signature_answer(rx_buf *out, const char *signature, int from, int to, const char *doc) {
    size_t sl = rx_one_line_len(signature);
    if (from < 0 || to < from || (size_t)to > sl) from = to = -1;
    char head[32];
    int hl = snprintf(head, sizeof head, "%d %d\n", from, to);
    if ((size_t)hl + sl + 1 > 65536) return;   /* a signature over 64 KB: no answer hides the help */
    rx_buf_add(out, head, (size_t)hl);
    rx_buf_add(out, signature, sl);
    rx_buf_add(out, "\n", 1);
    if (!doc) return;
    size_t room = 65536 - ((size_t)hl + sl + 1), dl = strlen(doc);
    if (dl > room) {
        dl = room;
        while (dl > 0 && ((unsigned char)doc[dl] & 0xc0) == 0x80) dl--;   /* not inside a character */
    }
    rx_buf_add(out, doc, dl);
}

/* ---- definition, references, symbols, code actions, rename and format */

#define RX_RECORDS 10000                        /* locations, symbols (12d, 12e) */
#define RX_RECORDS_BODY ((size_t)1 << 20)
#define RX_ACTIONS 1000                         /* code actions (12f), their edits included */
#define RX_ACTIONS_BODY ((size_t)4 << 20)
#define RX_WEDIT_FILES 1000                     /* a workspace edit (12g) */
#define RX_WEDIT_EDITS 100000
#define RX_WEDIT_BODY ((size_t)4 << 20)
#define RX_TEXT_MAX 1000                        /* a name, title, detail or preview (12d) */
#define RX_KIND_MAX 40                          /* a kind word */
#define RX_PATH_MAX 4096

/* how many of the n bytes of s fit in max, cut at the start of a character */
static size_t rx_cut(const char *s, size_t n, size_t max) {
    if (n <= max) return n;
    n = max;
    while (n > 0 && ((unsigned char)s[n] & 0xc0) == 0x80) n--;
    return n;
}

/* a line or column as rhun reads it: not negative */
static int rx_nn(int v) { return v < 0 ? 0 : v; }

static void rx_buf_clear(rx_buf *b) {
    b->len = 0;
    if (b->data) b->data[0] = 0;
}

/* n bytes of s with its line breaks as spaces: a one-line field */
static void rx_add_line(rx_buf *b, const char *s, size_t n) {
    size_t at = b->len;
    rx_buf_add(b, s, n);
    for (size_t i = at; i < b->len; i++)
        if (b->data[i] == '\n' || b->data[i] == '\r') b->data[i] = ' ';
}

/* a one-word field: s without spaces and control characters, at most max bytes, or dflt when
 * nothing is left */
static void rx_add_word_max(rx_buf *b, const char *s, const char *dflt, size_t max) {
    size_t at = b->len;
    for (; s && *s; s++)
        if ((unsigned char)*s > ' ' && *s != 0x7f) rx_buf_add(b, s, 1);
    if (b->len == at) rx_buf_str(b, dflt);
    b->len = at + rx_cut(b->data + at, b->len - at, max);
    b->data[b->len] = 0;
}

/* a kind word: at most 40 bytes */
static void rx_add_word(rx_buf *b, const char *s, const char *dflt) { rx_add_word_max(b, s, dflt, RX_KIND_MAX); }

/* PATH (12d): the path, or #DOC */
static const char *rx_target(const char *path, rx_doc doc, char *tmp, size_t cap) {
    if (path && *path) return path;
    snprintf(tmp, cap, "#%lld", (long long)doc);
    return tmp;
}

/* keeps the record added after mark when the body still fits in max with its "N" line; else
 * drops it, and every later one (what is kept stays in order, with nothing missing between) */
static void rx_keep(rx_buf *recs, size_t mark, int *n, int *full, size_t max) {
    if (recs->len + 16 > max) {
        recs->len = mark;
        recs->data[mark] = 0;
        *full = 1;
        return;
    }
    (*n)++;
}

static void rx_records_finish(rx_buf *recs, int *n, int *full, rx_buf *out) {
    rx_buf_fmt(out, "%d\n", *n);
    rx_buf_add(out, recs->data, recs->len);
    rx_buf_free(recs);
    *n = *full = 0;
}

static int rx_blank(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

void rx_location(rx_locations *l, const char *path, rx_doc doc, rx_range r, const char *preview) {
    if (l->n >= RX_RECORDS) l->full = 1;
    if (l->full) return;
    char tmp[32];
    const char *t = rx_target(path, doc, tmp, sizeof tmp), *p = preview ? preview : "";
    size_t tl = strlen(t), pl = strlen(p), mark = l->recs.len;
    if (tl > RX_PATH_MAX) return;
    while (pl && rx_blank(p[0])) p++, pl--;
    while (pl && rx_blank(p[pl - 1])) pl--;
    pl = rx_cut(p, pl, RX_TEXT_MAX);
    rx_buf_fmt(&l->recs, "%d %d %d %d %zu %zu\n", rx_nn(r.start.line), rx_nn(r.start.col), rx_nn(r.end.line),
               rx_nn(r.end.col), tl, pl);
    rx_buf_add(&l->recs, t, tl);
    rx_add_line(&l->recs, p, pl);
    rx_keep(&l->recs, mark, &l->n, &l->full, RX_RECORDS_BODY);
}

void rx_symbol(rx_symbols *l, rx_range r, const char *kind, int depth, const char *name, const char *detail) {
    if (l->n >= RX_RECORDS) l->full = 1;
    if (l->full) return;
    if (!name) name = "";
    if (!detail) detail = "";
    size_t nl = rx_cut(name, strlen(name), RX_TEXT_MAX), dl = rx_cut(detail, strlen(detail), RX_TEXT_MAX);
    size_t mark = l->recs.len;
    if (!nl) return;
    rx_buf_fmt(&l->recs, "%d %d %d %d ", rx_nn(r.start.line), rx_nn(r.start.col), rx_nn(r.end.line), rx_nn(r.end.col));
    rx_add_word(&l->recs, kind, "function");
    rx_buf_fmt(&l->recs, " %d %zu %zu\n", depth < 0 ? 0 : depth, nl, dl);
    rx_add_line(&l->recs, name, nl);
    rx_add_line(&l->recs, detail, dl);
    rx_keep(&l->recs, mark, &l->n, &l->full, RX_RECORDS_BODY);
}

void rx_locations_finish(rx_locations *l, rx_buf *out) { rx_records_finish(&l->recs, &l->n, &l->full, out); }
void rx_symbols_finish(rx_symbols *l, rx_buf *out) { rx_records_finish(&l->recs, &l->n, &l->full, out); }
void rx_actions_finish(rx_actions *l, rx_buf *out) { rx_records_finish(&l->recs, &l->n, &l->full, out); }

/* frees the edits; what failed stays */
static void rx_wedit_drop(rx_wedit *e) {
    rx_buf_free(&e->body);
    rx_buf_free(&e->group);
    rx_buf_free(&e->target);
    for (int i = 0; i < 3; i++) rx_buf_free(&e->ops[i]);
    rx_buf_free(&e->hashes);
    rx_buf_free(&e->label);
    e->files = e->edits = e->group_edits = e->open = 0;
    e->nops = e->confirm = e->hashed = 0;
    e->hash = 0;
}

void rx_wedit_free(rx_wedit *e) {
    rx_wedit_drop(e);
    e->version = 0;
    e->fail = NULL;
    e->why[0] = 0;
}

/* the first failure wins: the edit is dropped, and every later call does nothing */
static void rx_wedit_fail(rx_wedit *e, const char *code, const char *why) {
    if (e->fail) return;
    rx_wedit_drop(e);
    size_t n = strlen(why);
    if (n > sizeof e->why - 1) {
        n = sizeof e->why - 1;
        while (n > 0 && ((unsigned char)why[n] & 0xc0) == 0x80) n--;   /* not inside a character */
    }
    memmove(e->why, why, n);
    e->why[n] = 0;
    for (char *c = e->why; *c; c++)
        if (*c == '\n' || *c == '\r') *c = ' ';
    e->fail = code;
}

int rx_refuse(rx_wedit *e, const char *why) {
    e->fail = NULL;                         /* the handler's reason wins over a limit it ran into */
    rx_wedit_fail(e, "refused", why ? why : "");
    return -1;
}

/* the open file's header and edits go after the files before it; a hashed PRE's offset is kept
 * (hashes counts the groups a rhun without workspace-edits would refuse) */
static void rx_wedit_flush(rx_wedit *e) {
    if (!e->open) return;
    char v[32];
    if (e->version >= 0) snprintf(v, sizeof v, "%lld", (long long)e->version);
    else if (e->hashed) snprintf(v, sizeof v, "fnv1a64:%016llx", (unsigned long long)e->hash);
    else snprintf(v, sizeof v, "-");
    rx_buf_fmt(&e->body, "%zu ", e->target.len);
    if (e->version < 0 && e->hashed) {
        size_t at = e->body.len;
        rx_buf_add(&e->hashes, &at, sizeof at);
    }
    rx_buf_fmt(&e->body, "%s %d\n", v, e->group_edits);
    rx_buf_add(&e->body, e->target.data, e->target.len);
    rx_buf_add(&e->body, e->group.data, e->group.len);
    rx_buf_clear(&e->target);
    rx_buf_clear(&e->group);
    e->group_edits = e->open = e->hashed = 0;
}

/* what the body holds already is past 4 MB: its headers would only add to it */
static void rx_wedit_check(rx_wedit *e) {
    size_t n = e->body.len + e->target.len + e->group.len + e->ops[0].len + e->ops[1].len + e->ops[2].len + e->label.len;
    if (n > RX_WEDIT_BODY) rx_wedit_fail(e, "toobig", "over 4194304 bytes");
}

void rx_wedit_file(rx_wedit *e, const char *path, rx_doc doc, int64_t version) {
    if (e->fail) return;
    rx_wedit_flush(e);
    if (e->files + e->nops >= RX_WEDIT_FILES) {
        rx_wedit_fail(e, "toobig", e->nops ? "more than 1000 entries" : "more than 1000 files");
        return;
    }
    char tmp[32];
    const char *t = rx_target(path, doc, tmp, sizeof tmp);
    if (strlen(t) > RX_PATH_MAX) {
        rx_wedit_fail(e, "toobig", "a path over 4096 bytes");
        return;
    }
    rx_buf_str(&e->target, t);
    e->version = version < 0 ? -1 : version;
    e->files++;
    e->open = 1;
    rx_wedit_check(e);
}

void rx_wedit_file_hashed(rx_wedit *e, const char *path, const void *data, size_t len) {
    rx_wedit_file(e, path, 0, RX_ON_DISK);
    if (e->fail) return;
    e->hashed = 1;
    e->hash = rx_fnv1a64(data, len);
}

void rx_wedit_edit(rx_wedit *e, rx_range r, const char *text, size_t len) {
    if (e->fail) return;
    if (!e->open) {
        rx_wedit_fail(e, "syntax", "an edit before its file (rx_wedit_file)");
        return;
    }
    if (e->edits >= RX_WEDIT_EDITS) {
        rx_wedit_fail(e, "toobig", "more than 100000 edits");
        return;
    }
    if (len > RX_WEDIT_BODY) {
        rx_wedit_fail(e, "toobig", "over 4194304 bytes");
        return;
    }
    rx_buf_fmt(&e->group, "%d %d %d %d %zu\n", rx_nn(r.start.line), rx_nn(r.start.col), rx_nn(r.end.line),
               rx_nn(r.end.col), len);
    rx_buf_add(&e->group, text, len);
    e->edits++;
    e->group_edits++;
    rx_wedit_check(e);
}

/* a file operation: phase 0 creates, 1 renames, 2 deletes */
static void rx_wedit_op(rx_wedit *e, int phase, const char *word, const char *path, const char *to, int flags, int allowed) {
    if (e->fail) return;
    rx_wedit_flush(e);
    if (e->files + e->nops >= RX_WEDIT_FILES) {
        rx_wedit_fail(e, "toobig", "more than 1000 entries");
        return;
    }
    if (flags & ~allowed) {
        rx_wedit_fail(e, "syntax", "a flag this file operation does not take");
        return;
    }
    size_t pl = path ? strlen(path) : 0, tl = to ? strlen(to) : 0;
    if (!pl || pl > RX_PATH_MAX || (phase == 1 && (!tl || tl > RX_PATH_MAX))) {
        rx_wedit_fail(e, "toobig", "a path of 1 to 4096 bytes");
        return;
    }
    static const char *names[4] = {"overwrite", "ignore-existing", "recursive", "ignore-missing"};
    char f[64] = "";
    for (int i = 0; i < 4; i++)
        if (flags & (1 << i)) {
            if (*f) strcat(f, ",");
            strcat(f, names[i]);
        }
    rx_buf *b = &e->ops[phase];
    if (phase == 1) rx_buf_fmt(b, "%s %zu %zu %s\n", word, pl, tl, *f ? f : "-");
    else rx_buf_fmt(b, "%s %zu %s\n", word, pl, *f ? f : "-");
    rx_buf_add(b, path, pl);
    if (phase == 1) rx_buf_add(b, to, tl);
    e->nops++;
    rx_wedit_check(e);
}

void rx_wedit_create(rx_wedit *e, const char *path, int flags) {
    rx_wedit_op(e, 0, "create", path, NULL, flags, RX_OVERWRITE | RX_IGNORE_EXISTING);
}

void rx_wedit_rename(rx_wedit *e, const char *from, const char *to, int flags) {
    rx_wedit_op(e, 1, "rename", from, to, flags, RX_OVERWRITE | RX_IGNORE_EXISTING);
}

void rx_wedit_delete(rx_wedit *e, const char *path, int flags) {
    rx_wedit_op(e, 2, "delete", path, NULL, flags, RX_RECURSIVE | RX_IGNORE_MISSING);
}

void rx_wedit_label(rx_wedit *e, const char *label) {
    if (e->fail) return;
    rx_buf_clear(&e->label);
    if (!label) return;
    rx_add_line(&e->label, label, rx_cut(label, strlen(label), 200));
}

void rx_wedit_confirm(rx_wedit *e) {
    if (!e->fail) e->confirm = 1;
}

/* the body for a rhun that agreed workspace-edits (ws) and file-operations (ops); fixed, when not
 * NULL, names a body that keeps 12g's grammar whatever was agreed. What needs more than that
 * (file operations without both features; a hashed group, a label or confirm without
 * workspace-edits or in a fixed body) fails as unknown: nothing rhun would refuse is sent. */
static int rx_wedit_finish_caps(rx_wedit *e, rx_buf *out, int ws, int ops, const char *fixed) {
    if (!e->fail) rx_wedit_flush(e);
    const char *part = e->nops ? "file operations" : e->hashes.len ? "fnv1a64 precondition"
                     : e->label.len ? "label" : e->confirm ? "confirm record" : NULL;
    if (!e->fail && part) {
        char why[200];
        if (fixed) snprintf(why, sizeof why, "%s keeps 12g's grammar, which has no %s", fixed, part);
        else if (e->nops && !(ws && ops)) snprintf(why, sizeof why, "file operations need the workspace-edits and file-operations features");
        else if (!ws) snprintf(why, sizeof why, "the edit's %s needs the workspace-edits feature, which was not agreed", part);
        else why[0] = 0;
        if (why[0]) rx_wedit_fail(e, "unknown", why);
    }
    if (!e->fail) {
        char head[32], lh[32] = "";
        int hl = snprintf(head, sizeof head, "%d\n", e->files + e->nops);
        size_t total = (size_t)hl + e->body.len + e->ops[0].len + e->ops[1].len + e->ops[2].len;
        if (e->label.len) total += (size_t)snprintf(lh, sizeof lh, "label %zu\n", e->label.len) + e->label.len;
        if (e->confirm) total += 10;
        if (total > RX_WEDIT_BODY) {
            rx_wedit_fail(e, "toobig", "over 4194304 bytes");
        } else {
            rx_buf_add(out, head, (size_t)hl);
            rx_buf_add(out, e->ops[0].data, e->ops[0].len);
            rx_buf_add(out, e->body.data, e->body.len);
            rx_buf_add(out, e->ops[1].data, e->ops[1].len);
            rx_buf_add(out, e->ops[2].data, e->ops[2].len);
            if (e->label.len) {             /* label and confirm: refused above without workspace-edits */
                rx_buf_str(out, lh);
                rx_buf_add(out, e->label.data, e->label.len);
            }
            if (e->confirm) rx_buf_str(out, "confirm 0\n");
        }
    }
    rx_wedit_drop(e);
    return e->fail ? -1 : 0;
}

int rx_wedit_finish(rx_wedit *e, rx_buf *out) { return rx_wedit_finish_caps(e, out, 1, 1, NULL); }

int rx_wedit_finish_for(rx_ext *ext, rx_wedit *e, rx_buf *out) {
    int caps = rx_edit_caps(ext);
    return rx_wedit_finish_caps(e, out, caps & 1, (caps & 2) != 0, NULL);
}

/* a builder's caps as rx_run sets them (1, and 2 for workspace-edits, 4 for file-operations) from
 * rx_edit_caps' */
static int rx_builder_caps(int caps) { return 1 | (caps & 1 ? 2 : 0) | (caps & 2 ? 4 : 0); }

/* finishes an action's edit into eb for a builder's caps (0: the whole grammar) */
static int rx_action_edit(rx_wedit *edit, rx_buf *eb, int caps) {
    int ws = !caps || (caps & 2), ops = !caps || (caps & 4);
    return rx_wedit_finish_caps(edit, eb, ws, ops, NULL);
}

void rx_action(rx_actions *l, const char *kind, const char *title, rx_wedit *edit, const void *data, size_t len) {
    rx_buf eb = {NULL, 0, 0};
    int bad = edit && rx_action_edit(edit, &eb, l->caps) < 0;
    if (edit) rx_wedit_free(edit);
    if (!title) title = "";
    size_t tl = rx_cut(title, strlen(title), RX_TEXT_MAX), mark = l->recs.len;
    if (!tl) bad = 1;                       /* no title: nothing to list */
    if (!bad && l->n >= RX_ACTIONS) l->full = 1;
    if (bad || l->full) {
        rx_buf_free(&eb);
        return;
    }
    rx_add_word(&l->recs, kind, "quickfix");
    rx_buf_fmt(&l->recs, " %zu %zu %zu\n", tl, eb.len, len);
    rx_add_line(&l->recs, title, tl);
    rx_buf_add(&l->recs, eb.data, eb.len);
    rx_buf_add(&l->recs, data, len);
    rx_buf_free(&eb);
    rx_keep(&l->recs, mark, &l->n, &l->full, RX_ACTIONS_BODY);
}

rx_pos rx_word_start(const unsigned char *text, size_t len, rx_pos at) {
    size_t off = 0;
    for (int line = 1; line < at.line; line++) {
        const unsigned char *nl = (const unsigned char *)memchr(text + off, '\n', len - off);
        if (!nl) return at;
        off = (size_t)(nl - text) + 1;
    }
    size_t end = off + (size_t)(at.col - 1), s;
    if (end > len) end = len;
    for (s = end; s > off; s--) {
        unsigned char c = text[s - 1];
        if (!(c == '_' || c >= 0x80 || (c >= '0' && c <= '9') || ((c | 0x20) >= 'a' && (c | 0x20) <= 'z'))) break;
    }
    rx_pos p = {at.line, (int)(s - off) + 1};
    return p;
}

const char *rx_event_tail(const rx_event *ev, int n) {
    const char *p = ev->rest;
    for (int i = 0; i < n && p; i++) {
        p = strchr(p, ' ');
        if (p) p++;
    }
    return p ? p : "";
}

/* ---- protocol 1.1: the editor model */

static int rx_need(rx_ext *ext, const char *feature) {
    if (rx_has(ext, feature)) return 1;
    snprintf(ext->error, sizeof ext->error, "unknown %s was not agreed", feature);
    return 0;
}

int rx_select_many(rx_ext *ext, rx_doc doc, const rx_selection *sels, int n) {
    if (!rx_need(ext, "selections")) return -1;
    if (n < 1 || n > 512 || !sels) {
        snprintf(ext->error, sizeof ext->error, "toobig %d selections: 1 to 512", n);
        return -1;
    }
    rx_buf body = {NULL, 0, 0};
    for (int i = 0; i < n; i++)
        rx_buf_fmt(&body, "%d %d %d %d\n", sels[i].anchor.line, sels[i].anchor.col, sels[i].cursor.line, sels[i].cursor.col);
    char line[96];
    snprintf(line, sizeof line, "select-many %lld %d %zu", (long long)doc, n, body.len);
    int r = rx_request(ext, line, body.data, body.len, NULL, NULL);
    rx_buf_free(&body);
    return r;
}

/* reads a decimal int field: 1 when it is one */
static int rx_int_field(const char *s, int *out) {
    int64_t v;
    if (!rx_positive_num(s, 2147483647, &v)) return 0;
    *out = (int)v;
    return 1;
}

int rx_parse_selections(const rx_event *ev, rx_doc *doc, rx_selection *sels, int max) {
    int64_t d;
    int n;
    if (!ev || strcmp(ev->name, "selections") || ev->argc != 3 || !rx_positive_num(ev->argv[0], INT64_MAX, &d) ||
        !rx_int_field(ev->argv[1], &n) || n < 1 || n > 512 || !ev->body)
        return -1;
    const char *p = (const char *)ev->body, *end = p + ev->body_len;
    for (int i = 0; i < n; i++) {
        const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p));
        size_t ll = nl ? (size_t)(nl - p) : (size_t)(end - p);
        char l[64], *f[RX_MAXF];
        int v[4];
        if (ll >= sizeof l) return -1;
        memcpy(l, p, ll);
        l[ll] = 0;
        if (rx_split(l, f) != 4) return -1;
        for (int k = 0; k < 4; k++)
            if (!rx_int_field(f[k], &v[k])) return -1;
        if (i < max && sels) {
            sels[i].anchor.line = v[0], sels[i].anchor.col = v[1];
            sels[i].cursor.line = v[2], sels[i].cursor.col = v[3];
        }
        p = nl ? nl + 1 : end;
        if (!nl && i + 1 < n) return -1;
    }
    if (p != end) return -1;
    if (doc) *doc = d;
    return n < max ? n : max;
}

int rx_reveal(rx_ext *ext, rx_doc doc, rx_pos at, const char *where) {
    if (!rx_need(ext, "reveal")) return -1;
    if (!where || (strcmp(where, "top") && strcmp(where, "center") && strcmp(where, "nearest") &&
                   strcmp(where, "center-if-outside"))) {
        snprintf(ext->error, sizeof ext->error, "syntax where is top, center, nearest or center-if-outside");
        return -1;
    }
    char line[128];
    snprintf(line, sizeof line, "reveal %lld %d %d %s", (long long)doc, at.line, at.col, where);
    return rx_request(ext, line, NULL, 0, NULL, NULL);
}

/* a title: one line of at most 200 bytes, cut at a character */
static void rx_title(rx_buf *b, const char *t) {
    if (!t) t = "";
    rx_add_line(b, t, rx_cut(t, strlen(t), 200));
}

int rx_show_locations(rx_ext *ext, const char *title, rx_locations *locs) {
    rx_buf body = {NULL, 0, 0};
    if (!rx_need(ext, "reveal")) {
        if (locs) rx_buf_free(&locs->recs);
        return -1;
    }
    rx_title(&body, title);
    rx_buf_add(&body, "\n", 1);
    rx_locations none;
    memset(&none, 0, sizeof none);
    rx_locations_finish(locs ? locs : &none, &body);
    char line[64];
    snprintf(line, sizeof line, "show-locations %zu", body.len);
    int r = rx_request(ext, line, body.data, body.len, NULL, NULL);
    rx_buf_free(&body);
    return r;
}

static int64_t rx_diff_sides(rx_ext *ext, const char *left, const char *right, const char *title, int background) {
    if (!rx_need(ext, "diff")) return -1;
    rx_buf t = {NULL, 0, 0}, f = {NULL, 0, 0}, line = {NULL, 0, 0};
    rx_title(&t, title);
    rx_buf_fmt(&line, "diff %s %s %s %zu", left, right, background ? "background" : "show", t.len);
    int64_t tab = -1;
    if (rx_request(ext, line.data, t.data ? t.data : "", t.len, &f, NULL) == 0 &&
        (!rx_positive_num(f.data, INT64_MAX, &tab) || tab <= 0)) {
        snprintf(ext->error, sizeof ext->error, "syntax invalid diff reply");
        tab = -1;
    }
    rx_buf_free(&t);
    rx_buf_free(&f);
    rx_buf_free(&line);
    return tab;
}

int64_t rx_diff(rx_ext *ext, rx_doc left, rx_doc right, const char *title, int background) {
    char l[24], r[24];
    snprintf(l, sizeof l, "%lld", (long long)left);
    snprintf(r, sizeof r, "%lld", (long long)right);
    return rx_diff_sides(ext, l, r, title, background);
}

/* a URI where rhun takes a document: one field */
static int rx_uri_field(rx_ext *ext, const char *uri) {
    size_t n = uri ? strlen(uri) : 0;
    if (!n || n > 4128 || strpbrk(uri, " \n\r")) {
        snprintf(ext->error, sizeof ext->error, "syntax a URI field is 1 to 4128 bytes without spaces or line breaks");
        return 0;
    }
    return 1;
}

int64_t rx_diff_uris(rx_ext *ext, const char *left, const char *right, const char *title, int background) {
    if (!rx_need(ext, "diff") || !rx_uri_field(ext, left) || !rx_uri_field(ext, right)) return -1;
    return rx_diff_sides(ext, left, right, title, background);
}

int rx_reveal_uri(rx_ext *ext, const char *uri, rx_pos at, const char *where) {
    if (!rx_need(ext, "reveal") || !rx_uri_field(ext, uri)) return -1;
    if (!where || (strcmp(where, "top") && strcmp(where, "center") && strcmp(where, "nearest") &&
                   strcmp(where, "center-if-outside"))) {
        snprintf(ext->error, sizeof ext->error, "syntax where is top, center, nearest or center-if-outside");
        return -1;
    }
    rx_buf line = {NULL, 0, 0};
    rx_buf_fmt(&line, "reveal %s %d %d %s", uri, at.line, at.col, where);
    int r = rx_request(ext, line.data, NULL, 0, NULL, NULL);
    rx_buf_free(&line);
    return r;
}

int rx_diff_close(rx_ext *ext, int64_t tab) {
    if (!rx_need(ext, "diff")) return -1;
    char line[64];
    snprintf(line, sizeof line, "diff-close %lld", (long long)tab);
    return rx_request(ext, line, NULL, 0, NULL, NULL);
}

void rx_linked_range(rx_linked *l, rx_range r) {
    if (l->n <= 64) rx_buf_fmt(&l->ranges, "%d %d %d %d\n", rx_nn(r.start.line), rx_nn(r.start.col), rx_nn(r.end.line), rx_nn(r.end.col));
    l->n++;
}

void rx_linked_word(rx_linked *l, const char *chars) {
    size_t k = 0;
    for (; chars && *chars; chars++) {
        char c = *chars;
        if (c >= ' ' && c <= '~' && !memchr(l->word, c, k) && k < sizeof l->word - 1) l->word[k++] = c;
    }
    l->word[k] = 0;
    l->has_word = 1;
}

void rx_linked_finish(rx_linked *l, rx_buf *out) {
    if (l->n < 2 || l->n > 64) {
        rx_buf_str(out, "0\n");
    } else {
        rx_buf_fmt(out, "%d\n", l->n);
        rx_buf_add(out, l->ranges.data, l->ranges.len);
        if (l->has_word) {
            rx_buf_fmt(out, "word %zu\n", strlen(l->word));
            rx_buf_str(out, l->word);
        }
    }
    rx_buf_free(&l->ranges);
    l->n = l->has_word = 0;
}

/* ---- ordered changes onto rhun's phases */

static int rx_is_sep(char c) {
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

/* the length of p without separators at its end (a lone "/" stays) */
static size_t rx_trimmed(const char *p) {
    size_t n = strlen(p);
    while (n > 1 && rx_is_sep(p[n - 1])) n--;
    return n;
}

static int rx_same_path(const char *a, const char *b) {
    size_t n = rx_trimmed(a);
    return n == rx_trimmed(b) && strncmp(a, b, n) == 0;
}

/* the rest of path after the folder dir (from its separator), or NULL when it is not inside */
static const char *rx_under(const char *path, const char *dir) {
    size_t d = rx_trimmed(dir);
    if (strlen(path) > d && strncmp(path, dir, d) == 0 && rx_is_sep(path[d])) return path + d;
    return NULL;
}

int rx_locate(const rx_change *before, int n, const char *path, char *src, size_t cap, int *created) {
    rx_buf p = {NULL, 0, 0};
    rx_buf_str(&p, path);
    int r = 0;
    if (created) *created = 0;
    for (int i = n - 1; i >= 0 && r == 0; i--) {
        const rx_change *c = &before[i];
        const char *rest;
        if (c->kind == RX_CHANGE_RENAME) {
            if (rx_same_path(p.data, c->to)) {
                rx_buf_clear(&p);
                rx_buf_str(&p, c->path);
            } else if ((rest = rx_under(p.data, c->to)) != NULL) {
                rx_buf q = {NULL, 0, 0};
                rx_buf_add(&q, c->path, rx_trimmed(c->path));
                rx_buf_str(&q, rest);
                rx_buf_free(&p);
                p = q;
            } else if (rx_same_path(p.data, c->path) || rx_under(p.data, c->path)) {
                r = -1;
            }
        } else if (c->kind == RX_CHANGE_DELETE) {
            if (rx_same_path(p.data, c->path) || rx_under(p.data, c->path)) r = -1;
        } else if (c->kind == RX_CHANGE_CREATE && rx_same_path(p.data, c->path)) {
            if (created) *created = 1;
            break;
        }
    }
    if (r == 0 && src) snprintf(src, cap, "%s", p.data);
    rx_buf_free(&p);
    return r;
}

static int rx_pos_cmp(rx_pos a, rx_pos b) {
    if (a.line != b.line) return a.line < b.line ? -1 : 1;
    return a.col < b.col ? -1 : a.col > b.col;
}

/* one edit of a composed group: its range, its text (borrowed) and how it sorts */
typedef struct {
    rx_range r;
    const char *t;
    size_t n;
    rx_pos rank;
    int grp, idx;
} rx_cedit;

static int rx_cedit_cmp(const void *x, const void *y) {
    const rx_cedit *a = (const rx_cedit *)x, *b = (const rx_cedit *)y;
    int c = rx_pos_cmp(a->r.start, b->r.start);
    if (!c) c = rx_pos_cmp(a->r.end, b->r.end);
    if (!c) c = rx_pos_cmp(a->rank, b->rank);
    if (!c) c = a->grp - b->grp;
    if (!c) c = a->idx - b->idx;
    return c;
}

static rx_pos rx_advance(rx_pos p, const char *t, size_t n) {
    const char *last = NULL;
    int lines = 0;
    for (size_t i = 0; i < n; i++)
        if (t[i] == '\n') lines++, last = t + i;
    if (!last) p.col += (int)n;
    else p.line += lines, p.col = (int)(t + n - last);
    return p;
}

static rx_pos rx_forward(rx_pos p, rx_pos oe, rx_pos pe) {
    rx_pos q;
    if (p.line == oe.line) q.line = pe.line, q.col = pe.col + p.col - oe.col;
    else q.line = p.line - oe.line + pe.line, q.col = p.col;
    return q;
}

static rx_pos rx_back(rx_pos p, rx_pos oe, rx_pos pe) {
    rx_pos q;
    if (p.line == pe.line) q.line = oe.line, q.col = oe.col + p.col - pe.col;
    else q.line = p.line - pe.line + oe.line, q.col = p.col;
    return q;
}

/* a position of the text after base (sorted, with post[k] where each edit is there) in the text
 * before: a start where base removed text and put none in goes after it, an end before it; -1 in
 * text base put in */
static int rx_inv(const rx_cedit *b, const rx_range *post, int nb, rx_pos p, int start, rx_pos *out) {
    rx_pos oe = {1, 1}, pe = {1, 1};
    for (int k = 0; k < nb; k++) {
        rx_pos ps = post[k].start, pend = post[k].end;
        if (rx_pos_cmp(p, ps) < 0) break;
        if (!rx_pos_cmp(p, ps) && !(start && !rx_pos_cmp(ps, pend))) {
            *out = b[k].r.start;
            return 0;
        }
        if (rx_pos_cmp(p, pend) < 0) return -1;
        oe = b[k].r.end, pe = pend;
    }
    *out = rx_back(p, oe, pe);
    return 0;
}

/* base (*nb edits, owned array) then next (against the text base left): *base becomes the edits
 * of both against the first text, in an order rhun's stable sort keeps; why on failure */
static int rx_compose(rx_cedit **base, int *nb, const rx_change *next, char *why, size_t cap) {
    int n0 = *nb, nn = next->nedits;
    rx_cedit *b = *base;
    for (int k = 0; k < n0; k++) b[k].idx = k, b[k].rank = b[k].r.start, b[k].grp = 1;
    qsort(b, (size_t)n0, sizeof *b, rx_cedit_cmp);
    for (int k = 1; k < n0; k++)
        if (rx_pos_cmp(b[k].r.start, b[k - 1].r.end) < 0) {
            snprintf(why, cap, "two edits overlap");
            return -1;
        }
    rx_range *post = (rx_range *)malloc(sizeof *post * (size_t)(n0 ? n0 : 1));
    rx_cedit *all = (rx_cedit *)malloc(sizeof *all * (size_t)(n0 + nn ? n0 + nn : 1));
    if (!post || !all) abort();
    rx_pos oe = {1, 1}, pe = {1, 1};
    for (int k = 0; k < n0; k++) {
        post[k].start = rx_forward(b[k].r.start, oe, pe);
        post[k].end = rx_advance(post[k].start, b[k].t, b[k].n);
        oe = b[k].r.end, pe = post[k].end;
        all[k] = b[k];
        all[k].rank = post[k].start;
        all[k].idx = k;
    }
    int bad = 0;
    for (int k = 0; k < nn && !bad; k++) {
        rx_range r = next->ranges[k];
        if (rx_pos_cmp(r.end, r.start) < 0 || r.start.line < 1 || r.start.col < 1) {
            snprintf(why, cap, "an edit's range ends before it starts");
            bad = 1;
            break;
        }
        for (int j = 0; j < n0; j++) {
            rx_pos ps = post[j].start, pend = post[j].end;
            if ((rx_pos_cmp(r.start, pend) < 0 && rx_pos_cmp(ps, r.end) < 0) ||
                (!rx_pos_cmp(ps, pend) && rx_pos_cmp(r.start, ps) < 0 && rx_pos_cmp(ps, r.end) < 0)) {
                snprintf(why, cap, "an edit from %d:%d to %d:%d overlaps an earlier edit of the file", r.start.line,
                         r.start.col, r.end.line, r.end.col);
                bad = 1;
                break;
            }
        }
        if (bad) break;
        rx_cedit *c = &all[n0 + k];
        int same = !rx_pos_cmp(r.start, r.end);
        if (rx_inv(b, post, n0, r.start, 1, &c->r.start) < 0 ||
            (!same && rx_inv(b, post, n0, r.end, 0, &c->r.end) < 0)) {
            snprintf(why, cap, "an edit at %d:%d lands in text an earlier edit of the file put in", r.start.line, r.start.col);
            bad = 1;
            break;
        }
        if (same) c->r.end = c->r.start;
        c->t = next->texts[k];
        c->n = next->lens ? next->lens[k] : strlen(next->texts[k]);
        c->rank = r.start;
        c->grp = 0;
        c->idx = k;
    }
    free(post);
    if (bad) {
        free(all);
        return -1;
    }
    qsort(all, (size_t)(n0 + nn), sizeof *all, rx_cedit_cmp);
    free(b);
    *base = all;
    *nb = n0 + nn;
    return 0;
}

int rx_wedit_from_ordered(rx_wedit *out, const rx_change *changes, int n, int *origins, int *bad, char *why, size_t cap) {
    typedef struct {
        char *path;
        int64_t version;
        int hashed, first, nedits;
        uint64_t hash;
        rx_cedit *edits;
    } group;
    group *gs = (group *)calloc((size_t)(n ? n : 1), sizeof *gs);
    int ng = 0, r = 0, at = 0;
    char tmp[256];
    if (!gs) abort();
    if (!why || !cap) why = tmp, cap = sizeof tmp;
    why[0] = 0;
    for (int i = 0; i < n && !r; i++) {
        const rx_change *c = &changes[i];
        const char *mine[2] = {c->path, c->kind == RX_CHANGE_RENAME ? c->to : NULL};
        int holds = c->kind == RX_CHANGE_RENAME || c->kind == RX_CHANGE_DELETE;
        at = i;
        if (c->kind < RX_CHANGE_EDIT || c->kind > RX_CHANGE_DELETE) {
            snprintf(why, cap, "an unknown kind of change");
            r = -1;
            break;
        }
        for (int m = 0; c->kind != RX_CHANGE_EDIT && m < 2 && !r; m++) {
            const char *p = mine[m];
            if (m == 1 && c->kind != RX_CHANGE_RENAME) break;
            if (!p || !*p) {
                snprintf(why, cap, "a file operation without a path");
                r = -1;
                break;
            }
            for (int j = 0; j < i && !r; j++) {
                const rx_change *o = &changes[j];
                if (o->kind == RX_CHANGE_EDIT) continue;
                const char *theirs[2] = {o->path, o->kind == RX_CHANGE_RENAME ? o->to : NULL};
                for (int t = 0; t < 2 && theirs[t] && !r; t++) {
                    if (rx_same_path(p, theirs[t])) {
                        snprintf(why, cap, "%s is the path of two file operations", p);
                        r = -1;
                    } else if ((holds && rx_under(theirs[t], p)) || (o->kind != RX_CHANGE_CREATE && rx_under(p, theirs[t]))) {
                        snprintf(why, cap, "%s and a folder another file operation renames or deletes", p);
                        r = -1;
                    }
                }
            }
        }
        if (r) break;
        if (c->kind == RX_CHANGE_EDIT) {
            char src[4200];
            int created;
            if (rx_locate(changes, i, c->path, src, sizeof src, &created) < 0) {
                snprintf(why, cap, "an edit to %s after it was renamed away or deleted", c->path);
                r = -1;
                break;
            }
            int g = -1;
            for (int k = 0; k < ng; k++)
                if (rx_same_path(gs[k].path, src)) g = k;
            if (g >= 0) {
                char w2[200];
                if (rx_compose(&gs[g].edits, &gs[g].nedits, c, w2, sizeof w2) < 0) {
                    snprintf(why, cap, "%s: %s", src, w2);
                    r = -1;
                }
                continue;
            }
            group *ng_ = &gs[ng++];
            ng_->path = (char *)malloc(strlen(src) + 1);
            if (!ng_->path) abort();
            strcpy(ng_->path, src);
            ng_->version = created ? RX_ON_DISK : c->version;
            ng_->hashed = created ? 0 : c->hashed;
            ng_->hash = c->hash;
            ng_->first = i;
            ng_->nedits = c->nedits;
            ng_->edits = (rx_cedit *)calloc((size_t)(c->nedits ? c->nedits : 1), sizeof(rx_cedit));
            if (!ng_->edits) abort();
            for (int k = 0; k < c->nedits; k++) {
                ng_->edits[k].r = c->ranges[k];
                ng_->edits[k].t = c->texts[k];
                ng_->edits[k].n = c->lens ? c->lens[k] : strlen(c->texts[k]);
            }
        } else if (c->kind == RX_CHANGE_CREATE || c->kind == RX_CHANGE_RENAME) {
            const char *p = c->kind == RX_CHANGE_CREATE ? c->path : c->to;
            for (int k = 0; k < ng && !r; k++)
                if (rx_same_path(gs[k].path, p)) {
                    snprintf(why, cap, "%s is %s after an edit of it", p, c->kind == RX_CHANGE_CREATE ? "created" : "renamed onto");
                    r = -1;
                }
        }
    }
    if (!r) {
        /* creates, the groups (those of a path deleted afterwards left out), renames, deletes */
        int entry = 0;
        for (int phase = 0; phase < 4; phase++) {
            if (phase == 1) {
                for (int k = 0; k < ng; k++) {
                    int gone = 0;
                    for (int j = 0; j < n; j++)
                        if (changes[j].kind == RX_CHANGE_DELETE && rx_same_path(changes[j].path, gs[k].path)) gone = 1;
                    if (gone) continue;
                    if (gs[k].hashed && gs[k].version < 0) {
                        rx_wedit_file(out, gs[k].path, 0, RX_ON_DISK);
                        out->hashed = !out->fail;
                        out->hash = gs[k].hash;
                    } else {
                        rx_wedit_file(out, gs[k].path, 0, gs[k].version);
                    }
                    for (int m = 0; m < gs[k].nedits; m++) rx_wedit_edit(out, gs[k].edits[m].r, gs[k].edits[m].t, gs[k].edits[m].n);
                    if (origins) origins[entry] = gs[k].first;
                    entry++;
                }
                continue;
            }
            int kind = phase == 0 ? RX_CHANGE_CREATE : phase == 2 ? RX_CHANGE_RENAME : RX_CHANGE_DELETE;
            for (int j = 0; j < n; j++) {
                const rx_change *c = &changes[j];
                if (c->kind != kind) continue;
                if (kind == RX_CHANGE_CREATE) rx_wedit_create(out, c->path, c->flags);
                else if (kind == RX_CHANGE_RENAME) rx_wedit_rename(out, c->path, c->to, c->flags);
                else rx_wedit_delete(out, c->path, c->flags);
                if (origins) origins[entry] = j;
                entry++;
            }
        }
        if (out->fail) {
            snprintf(why, cap, "%s", out->why);
            at = -1;
            r = -1;
        }
    }
    for (int k = 0; k < ng; k++) {
        free(gs[k].path);
        free(gs[k].edits);
    }
    free(gs);
    if (r && bad) *bad = at;
    return r;
}

/* ---- protocol 1.1 language features: query-stream, provider-scope, complete-items, signature-list */

void rx_resolve_answer(rx_buf *out, const char *detail, const char *doc, const rx_range *edits,
                       const char *const *texts, int nedits, int after_signature, int after_complete) {
    if (detail) {
        size_t n = strlen(detail);
        rx_buf_fmt(out, "detail %zu\n", n);
        rx_add_line(out, detail, n);
    }
    if (doc && *doc) {
        size_t n = strlen(doc);
        rx_buf_fmt(out, "doc %zu\n", n);
        rx_buf_add(out, doc, n);
    }
    for (int i = 0; i < nedits; i++) {
        size_t n = strlen(texts[i]);
        rx_buf_fmt(out, "edit %d %d %d %d %zu\n", edits[i].start.line, edits[i].start.col, edits[i].end.line,
                   edits[i].end.col, n);
        rx_buf_add(out, texts[i], n);
    }
    if (after_signature) rx_buf_str(out, "after signature 0\n");
    if (after_complete) rx_buf_str(out, "after complete 0\n");
}

void rx_signatures_answer(rx_buf *out, const rx_signature_info *sigs, int n, int active) {
    if (n > 32) n = 32;
    if (!sigs || n <= 0) return;
    if (active < 0 || active >= n) active = 0;
    rx_buf b = {NULL, 0, 0};
    int docs = 0;
    rx_buf_fmt(&b, "%d %d\n", n, active);
    for (int i = 0; i < n; i++) {
        const char *label = sigs[i].label ? sigs[i].label : "", *doc = sigs[i].doc ? sigs[i].doc : "";
        size_t ll = strlen(label), dl = strlen(doc);
        int from = sigs[i].from, to = sigs[i].to;
        if (from < 0 || to < from || (size_t)to > ll) from = to = -1;
        rx_buf_fmt(&b, "%d %d %zu %zu\n", from, to, ll, dl);
        rx_add_line(&b, label, ll);
        rx_buf_add(&b, doc, dl);
        docs += dl > 0;
    }
    if (b.len <= 65536) {
        rx_buf_add(out, b.data, b.len);
    } else if (n > 1 || docs) {         /* the documentation goes, then overloads, until it fits */
        rx_signature_info fewer[32];
        int m = n / 2 > 1 ? n / 2 : 1;
        for (int i = 0; i < m; i++) {
            fewer[i] = sigs[i];
            fewer[i].doc = NULL;
        }
        rx_signatures_answer(out, fewer, m, 0);
    }                                   /* else a signature that does not fit on its own: none */
    rx_buf_free(&b);
}

/* whether rx_error's code is code */
static int rx_error_is(rx_ext *ext, const char *code) {
    size_t n = strlen(code);
    return strncmp(ext->error, code, n) == 0 && (ext->error[n] == ' ' || ext->error[n] == 0);
}

int rx_stream_cancelled(const rx_stream *s) {
    return !s || !s->ext || s->ext->gone || s->ext->query_id != s->id || s->ext->query_cancelled;
}

static int rx_stream_check(rx_stream *s) {
    if (!s || !s->ext) return -1;
    if (!rx_has(s->ext, "query-stream")) {
        snprintf(s->ext->error, sizeof s->ext->error, "unknown query-stream was not agreed");
        return -1;
    }
    if (rx_stream_cancelled(s)) {
        snprintf(s->ext->error, sizeof s->ext->error, "cancelled rhun no longer needs the answer");
        return -1;
    }
    return 0;
}

int rx_stream_part(rx_stream *s, const void *body, size_t len) {
    if (rx_stream_check(s) < 0) return -1;
    char line[96];
    snprintf(line, sizeof line, "query-part %lld %zu", (long long)s->id, len);
    int r = rx_request(s->ext, line, body, len, NULL, NULL);
    if (r < 0 && (rx_error_is(s->ext, "cancelled") || rx_error_is(s->ext, "toobig"))) s->ext->query_cancelled = 1;
    return r;
}

int rx_stream_locations(rx_stream *s, rx_locations *l) {
    rx_buf b = {NULL, 0, 0};
    rx_locations_finish(l, &b);
    int r = rx_stream_part(s, b.data, b.len);
    rx_buf_free(&b);
    return r;
}

int rx_stream_items(rx_stream *s, rx_list *l) {
    rx_buf b = {NULL, 0, 0};
    rx_list_finish(l, &b);
    int r = rx_stream_part(s, b.data, b.len);
    rx_buf_free(&b);
    return r;
}

int rx_stream_progress(rx_stream *s, int percent, const char *text) {
    if (rx_stream_check(s) < 0) return -1;
    char p[16] = "-", line[96];
    if (percent >= 0) snprintf(p, sizeof p, "%d", percent > 100 ? 100 : percent);
    if (!text) text = "";
    rx_buf t = {NULL, 0, 0};
    rx_add_line(&t, text, rx_cut(text, strlen(text), 200));
    snprintf(line, sizeof line, "query-progress %lld %s %zu", (long long)s->id, p, t.len);
    int r = rx_request(s->ext, line, t.data, t.len, NULL, NULL);
    if (r < 0 && rx_error_is(s->ext, "cancelled")) s->ext->query_cancelled = 1;
    rx_buf_free(&t);
    return r;
}

int rx_provides(rx_ext *ext, const char *key, const char *values) {
    if (!rx_has(ext, "provider-scope")) {
        snprintf(ext->error, sizeof ext->error, "unknown provider-scope was not agreed");
        return -1;
    }
    if (!key || !*key || strpbrk(key, " \r\n") || !values || !*values || strpbrk(values, "\r\n")) {
        snprintf(ext->error, sizeof ext->error, "syntax a key is one word and values one line");
        return -1;
    }
    rx_buf line = {NULL, 0, 0};
    rx_buf_fmt(&line, "provides %s %s", key, values);
    int r = rx_request(ext, line.data, NULL, 0, NULL, NULL);
    rx_buf_free(&line);
    return r;
}

/* ---- protocol 1.1 language features: navigation, prepare-rename, partial-format, code-actions */

#define RX_HKIND_MAX 128                        /* a hierarchical kind */
#define RX_ARGS_MAX 8192                        /* a command reference's args */
#define RX_ACTION_RESOLVE_MAX ((size_t)16 << 10)

void rx_prepare_rename_answer(rx_buf *out, rx_range r, const char *placeholder) {
    if (!placeholder) placeholder = "";
    size_t n = rx_cut(placeholder, strlen(placeholder), 200);
    rx_buf_fmt(out, "%d %d %d %d %zu\n", rx_nn(r.start.line), rx_nn(r.start.col), rx_nn(r.end.line),
               rx_nn(r.end.col), n);
    rx_add_line(out, placeholder, n);
}

int rx_refuse_answer(rx_buf *out, const char *why) {
    if (!why) why = "";
    size_t n = strlen(why);
    while (n && rx_blank(why[0])) why++, n--;
    while (n && rx_blank(why[n - 1])) n--;
    rx_buf_clear(out);
    rx_add_line(out, why, rx_cut(why, n, 1000));
    return -1;
}

int rx_kind_wanted(const char *kinds, const char *kind) {
    if (!kinds || !*kinds || strcmp(kinds, "-") == 0) return 1;
    if (!kind) kind = "";
    size_t kl = strlen(kind);
    for (const char *k = kinds; *k;) {
        const char *end = strchr(k, ',');
        size_t n = end ? (size_t)(end - k) : strlen(k);
        if (n && kl >= n && strncmp(kind, k, n) == 0 && (kind[n] == 0 || kind[n] == '.')) return 1;
        if (!end) break;
        k = end + 1;
    }
    return 0;
}

void rx_code_action(rx_code_actions *l, const char *kind, const char *title, rx_wedit *edit,
                    const rx_action_extra *x) {
    rx_buf eb = {NULL, 0, 0}, xb = {NULL, 0, 0}, kb = {NULL, 0, 0};
    int bad = edit && rx_action_edit(edit, &eb, l->caps) < 0;
    if (edit) rx_wedit_free(edit);
    if (!title) title = "";
    size_t tl = rx_cut(title, strlen(title), RX_TEXT_MAX), mark = l->recs.len;
    rx_add_word_max(&kb, kind, "quickfix", RX_HKIND_MAX);
    int does = eb.len > 0;
    if (x) {
        if (x->preferred) rx_buf_str(&xb, "preferred 0\n");
        if (x->disabled && *x->disabled) {
            size_t n = rx_cut(x->disabled, strlen(x->disabled), 1000);
            rx_buf_fmt(&xb, "disabled %zu\n", n);
            rx_add_line(&xb, x->disabled, n);
            does = 1;
        }
        if (x->resolve && x->resolve_len && x->resolve_len <= RX_ACTION_RESOLVE_MAX) {
            rx_buf_fmt(&xb, "resolve %zu\n", x->resolve_len);
            rx_buf_add(&xb, x->resolve, x->resolve_len);
            does = 1;
        }
        if (x->command && rx_command_ref(&xb, x->command, x->args, x->args_len) == 0) does = 1;
        if (x->fixes && *x->fixes) {
            size_t n = strlen(x->fixes);
            rx_buf_fmt(&xb, "fixes %zu\n", n);
            rx_add_line(&xb, x->fixes, n);
        }
    }
    /* no title, nothing to do, a kind not asked for: left out */
    if (!tl || !does || !rx_kind_wanted(l->kinds, kb.data)) bad = 1;
    if (!bad && l->n >= RX_ACTIONS) l->full = 1;
    if (!bad && !l->full) {
        rx_buf_add(&l->recs, kb.data, kb.len);
        rx_buf_fmt(&l->recs, " %zu %zu %zu\n", tl, eb.len, xb.len);
        rx_add_line(&l->recs, title, tl);
        rx_buf_add(&l->recs, eb.data, eb.len);
        rx_buf_add(&l->recs, xb.data, xb.len);
        rx_keep(&l->recs, mark, &l->n, &l->full, RX_ACTIONS_BODY);
    }
    rx_buf_free(&eb);
    rx_buf_free(&xb);
    rx_buf_free(&kb);
}

void rx_code_actions_finish(rx_code_actions *l, rx_buf *out) { rx_records_finish(&l->recs, &l->n, &l->full, out); }

int rx_resolved_finish(rx_resolved *r, rx_buf *out) {
    rx_buf eb = {NULL, 0, 0};
    int bad = r->edit.fail || ((r->edit.files || r->edit.nops) && rx_action_edit(&r->edit, &eb, r->caps) < 0);
    if (!bad) {
        rx_buf_fmt(out, "%zu %zu\n", eb.len, r->command.len);
        rx_buf_add(out, eb.data, eb.len);
        rx_buf_add(out, r->command.data, r->command.len);
    }
    rx_buf_free(&eb);
    rx_buf_free(&r->command);
    return bad ? -1 : 0;
}

/* ---- tests */

/* s is one line of at most max bytes (and not empty unless allowed) */
static int rx_test_text(const char *s, size_t max, int allow_empty) {
    size_t n = s ? strlen(s) : 0;
    if (n > max || (!allow_empty && n == 0)) return 0;
    return !s || strpbrk(s, "\n\r") == NULL;
}

static void rx_test_fail(const char **fail, char *why, size_t cap, const char *code, const char *text) {
    if (*fail) return;
    size_t n = strlen(text);
    if (n > cap - 1) {
        n = cap - 1;
        while (n > 0 && ((unsigned char)text[n] & 0xc0) == 0x80) n--;
    }
    memmove(why, text, n);
    why[n] = 0;
    for (char *c = why; *c; c++)
        if (*c == '\n' || *c == '\r') *c = ' ';
    *fail = code;
}

void rx_test_item(rx_test_items *l, const char *kind, const char *id, const char *label, const char *path,
                  const rx_range *range, const char *flags, const char *desc, const char *error) {
    if (l->fail) return;
    label = label ? label : "";
    path = path ? path : "";
    desc = desc ? desc : "";
    error = error ? error : "";
    if (!kind || (strcmp(kind, "group") && strcmp(kind, "file") && strcmp(kind, "suite") && strcmp(kind, "test"))) {
        rx_test_fail(&l->fail, l->why, sizeof l->why, "syntax", "a bad item kind");
        return;
    }
    if (!rx_test_text(id, 512, 0) || !rx_test_text(label, 200, 1) || !rx_test_text(path, 4095, 1) ||
        !rx_test_text(desc, 200, 1) || strlen(error) > 1000 ||
        (flags && *flags && strcmp(flags, "lazy") && strcmp(flags, "nodebug") && strcmp(flags, "lazy,nodebug"))) {
        rx_test_fail(&l->fail, l->why, sizeof l->why, "syntax", "a bad item");
        return;
    }
    if (l->n >= 10000) {
        rx_test_fail(&l->fail, l->why, sizeof l->why, "toobig", "10000 items per body");
        return;
    }
    int l1 = 0, c1 = 0, l2 = 0, c2 = 0;
    if (range) {
        l1 = range->start.line, c1 = range->start.col, l2 = range->end.line, c2 = range->end.col;
        if (l1 < 1 || c1 < 1 || c2 < 1 || l2 < l1 || (l2 == l1 && c2 < c1) || !*path) {
            rx_test_fail(&l->fail, l->why, sizeof l->why, "badpos", "a bad item range");
            return;
        }
    }
    rx_buf extra = {NULL, 0, 0};
    if (*desc) rx_buf_fmt(&extra, "desc %zu\n%s", strlen(desc), desc);
    if (*error) rx_buf_fmt(&extra, "error %zu\n%s", strlen(error), error);
    rx_buf_fmt(&l->recs, "%s %d %d %d %d %s %zu %zu %zu %zu\n", kind, l1, c1, l2, c2,
               flags && *flags ? flags : "-", strlen(id), strlen(label), strlen(path), extra.len);
    rx_buf_str(&l->recs, id);
    rx_buf_str(&l->recs, label);
    rx_buf_str(&l->recs, path);
    if (extra.len) rx_buf_add(&l->recs, extra.data, extra.len);
    rx_buf_free(&extra);
    l->n++;
}

int rx_test_refuse(rx_test_items *l, const char *why) {
    l->fail = NULL;
    rx_buf_free(&l->recs);
    l->n = 0;
    rx_test_fail(&l->fail, l->why, sizeof l->why, "refused", why ? why : "");
    return 1;
}

int rx_test_items_finish(rx_test_items *l, const char *parent, rx_buf *out) {
    parent = parent ? parent : "";
    if (!l->fail && *parent && !rx_test_text(parent, 512, 0))
        rx_test_fail(&l->fail, l->why, sizeof l->why, "syntax", "a bad parent id");
    if (!l->fail && l->recs.len > (4u << 20))
        rx_test_fail(&l->fail, l->why, sizeof l->why, "toobig", "4 MiB per body");
    if (l->fail) return -1;
    rx_buf_fmt(out, "%zu %d\n", strlen(parent), l->n);
    rx_buf_str(out, parent);
    if (l->recs.len) rx_buf_add(out, l->recs.data, l->recs.len);
    return 0;
}

void rx_test_items_free(rx_test_items *l) {
    rx_buf_free(&l->recs);
    l->n = 0;
    l->fail = NULL;
    l->why[0] = 0;
}

/* a request whose body was built here: fails locally with code and why when it could not be */
static int rx_test_local(rx_ext *ext, const char *code, const char *why) {
    snprintf(ext->error, sizeof ext->error, "%s %s", code, why);
    return -1;
}

static int rx_tests_agreed(rx_ext *ext) {
    if (rx_has(ext, "tests")) return 1;
    rx_test_local(ext, "unknown", "tests was not agreed");
    return 0;
}

int rx_test_items_send(rx_ext *ext, const char *parent, rx_test_items *l, int replace) {
    rx_buf body = {NULL, 0, 0}, line = {NULL, 0, 0};
    int r;
    if (!rx_tests_agreed(ext)) r = -1;
    else if (rx_test_items_finish(l, parent, &body) < 0) r = rx_test_local(ext, l->fail, l->why);
    else {
        rx_buf_fmt(&line, "test-items %s %zu", replace ? "replace" : "add", body.len);
        r = rx_request(ext, line.data, body.data, body.len, NULL, NULL);
    }
    rx_buf_free(&body);
    rx_buf_free(&line);
    rx_test_items_free(l);
    return r;
}

int rx_test_remove(rx_ext *ext, const char *const *ids, int n) {
    if (!rx_tests_agreed(ext)) return -1;
    rx_buf body = {NULL, 0, 0}, line = {NULL, 0, 0};
    for (int i = 0; i < n; i++) {
        if (!rx_test_text(ids[i], 512, 0)) {
            rx_buf_free(&body);
            return rx_test_local(ext, "syntax", "a bad id");
        }
        rx_buf_str(&body, ids[i]);
        rx_buf_add(&body, "\n", 1);
    }
    rx_buf_fmt(&line, "test-remove %d %zu", n, body.len);
    int r = rx_request(ext, line.data, body.data, body.len, NULL, NULL);
    rx_buf_free(&body);
    rx_buf_free(&line);
    return r;
}

/* the open record goes into recs */
static void rx_test_close(rx_test_results *r) {
    if (!r->open) return;
    r->open = 0;
    if (r->fail) return;
    char ms[32];
    if (r->state && r->ms >= 0) snprintf(ms, sizeof ms, "%lld", r->ms);
    else snprintf(ms, sizeof ms, "-");
    rx_buf_fmt(&r->recs, "%s %s %zu %zu\n", r->state ? r->state : "-", ms, r->item.len, r->extra.len);
    if (r->item.len) rx_buf_add(&r->recs, r->item.data, r->item.len);
    if (r->extra.len) rx_buf_add(&r->recs, r->extra.data, r->extra.len);
    r->extra.len = 0;
    r->item.len = 0;
    r->n++;
}

void rx_test_result(rx_test_results *r, const char *item, const char *state, long long ms) {
    static const char *states[] = {"queued", "started", "passed", "failed", "errored", "skipped"};
    rx_test_close(r);
    if (r->fail) return;
    item = item ? item : "";
    const char *st = NULL;
    for (size_t i = 0; state && i < sizeof states / sizeof *states; i++)
        if (strcmp(state, states[i]) == 0) st = states[i];
    if ((state && !st) || !rx_test_text(item, 512, 1) || (!*item && st && strcmp(st, "errored") != 0)) {
        rx_test_fail(&r->fail, r->why, sizeof r->why, "syntax", "a bad result record");
        return;
    }
    if (r->n >= 2000) {
        rx_test_fail(&r->fail, r->why, sizeof r->why, "toobig", "2000 records per body");
        return;
    }
    r->open = 1;
    r->state = st;
    r->ms = ms;
    r->item.len = 0;
    rx_buf_str(&r->item, item);
}

static void rx_test_extra(rx_test_results *r, const char *head, const void *data, size_t len) {
    if (r->fail) return;
    if (!r->open) {
        rx_test_fail(&r->fail, r->why, sizeof r->why, "syntax", "an extra before its record");
        return;
    }
    rx_buf_fmt(&r->extra, "%s %zu\n", head, len);
    rx_buf_add(&r->extra, data, len);
}

void rx_test_message(rx_test_results *r, const char *text, size_t len) { rx_test_extra(r, "message", text, len); }
void rx_test_expected(rx_test_results *r, const void *data, size_t len) { rx_test_extra(r, "expected", data, len); }
void rx_test_actual(rx_test_results *r, const void *data, size_t len) { rx_test_extra(r, "actual", data, len); }
void rx_test_output(rx_test_results *r, const void *data, size_t len) { rx_test_extra(r, "output", data, len); }

void rx_test_location(rx_test_results *r, const char *path, rx_range g) {
    char head[96];
    snprintf(head, sizeof head, "location %d %d %d %d", g.start.line, g.start.col, g.end.line, g.end.col);
    rx_test_extra(r, head, path, strlen(path));
}

int rx_test_results_finish(rx_test_results *r, rx_buf *out) {
    rx_test_close(r);
    if (!r->fail && r->recs.len + 16 > (1u << 20))
        rx_test_fail(&r->fail, r->why, sizeof r->why, "toobig", "1 MiB per body");
    if (r->fail) return -1;
    rx_buf_fmt(out, "%d\n", r->n);
    if (r->recs.len) rx_buf_add(out, r->recs.data, r->recs.len);
    return 0;
}

void rx_test_results_free(rx_test_results *r) {
    rx_buf_free(&r->recs);
    rx_buf_free(&r->extra);
    rx_buf_free(&r->item);
    r->n = r->open = 0;
    r->state = NULL;
    r->fail = NULL;
    r->why[0] = 0;
}

int rx_test_update(rx_ext *ext, int64_t run, rx_test_results *r) {
    rx_buf body = {NULL, 0, 0}, line = {NULL, 0, 0};
    int res;
    if (!rx_tests_agreed(ext)) res = -1;
    else if (rx_test_results_finish(r, &body) < 0) res = rx_test_local(ext, r->fail, r->why);
    else {
        rx_buf_fmt(&line, "test-update %lld %zu", (long long)run, body.len);
        res = rx_request(ext, line.data, body.data, body.len, NULL, NULL);
    }
    rx_buf_free(&body);
    rx_buf_free(&line);
    rx_test_results_free(r);
    return res;
}

int rx_test_end(rx_ext *ext, int64_t run) {
    if (!rx_tests_agreed(ext)) return -1;
    char line[64];
    snprintf(line, sizeof line, "test-end %lld", (long long)run);
    return rx_request(ext, line, NULL, 0, NULL, NULL);
}

static void rx_coverage_close(rx_test_coverage *c) {
    if (!c->open) return;
    c->open = 0;
    rx_buf_fmt(&c->recs, "%zu %d\n", c->path.len, c->nranges);
    if (c->path.len) rx_buf_add(&c->recs, c->path.data, c->path.len);
    if (c->ranges.len) rx_buf_add(&c->recs, c->ranges.data, c->ranges.len);
    c->path.len = c->ranges.len = 0;
    c->nranges = 0;
    c->files++;
}

void rx_coverage_file(rx_test_coverage *c, const char *path) {
    rx_coverage_close(c);
    c->open = 1;
    c->path.len = 0;
    rx_buf_str(&c->path, path);
}

void rx_coverage_lines(rx_test_coverage *c, int from, int to, long long count) {
    if (!c->open) return;
    rx_buf_fmt(&c->ranges, "%d %d %lld\n", from, to, count);
    c->nranges++;
}

void rx_test_coverage_finish(rx_test_coverage *c, rx_buf *out) {
    rx_coverage_close(c);
    rx_buf_fmt(out, "%d\n", c->files);
    if (c->recs.len) rx_buf_add(out, c->recs.data, c->recs.len);
}

void rx_test_coverage_free(rx_test_coverage *c) {
    rx_buf_free(&c->recs);
    rx_buf_free(&c->ranges);
    rx_buf_free(&c->path);
    c->files = c->nranges = c->open = 0;
}

int rx_test_coverage_send(rx_ext *ext, int64_t run, rx_test_coverage *c) {
    rx_buf body = {NULL, 0, 0}, line = {NULL, 0, 0};
    int r = -1;
    if (rx_tests_agreed(ext)) {
        rx_test_coverage_finish(c, &body);
        if (body.len > (4u << 20)) r = rx_test_local(ext, "toobig", "4 MiB per message: send per file");
        else {
            rx_buf_fmt(&line, "test-coverage %lld %zu", (long long)run, body.len);
            r = rx_request(ext, line.data, body.data, body.len, NULL, NULL);
        }
    }
    rx_buf_free(&body);
    rx_buf_free(&line);
    rx_test_coverage_free(c);
    return r;
}

int64_t rx_test_exec(rx_ext *ext, int64_t run, const char *program, const char *dir,
                     const char *const *argv, int argc, const char *const *env, int envc) {
    if (!rx_tests_agreed(ext)) return -1;
    dir = dir ? dir : "";
    rx_buf body = {NULL, 0, 0}, line = {NULL, 0, 0}, fields = {NULL, 0, 0};
    rx_buf_fmt(&body, "%zu %zu %d %d\n", strlen(program), strlen(dir), argc, envc);
    rx_buf_str(&body, program);
    rx_buf_str(&body, dir);
    for (int i = 0; i < argc; i++) {
        rx_buf_fmt(&body, "%zu\n", strlen(argv[i]));
        rx_buf_str(&body, argv[i]);
    }
    for (int i = 0; i < envc; i++) {
        rx_buf_fmt(&body, "%zu\n", strlen(env[i]));
        rx_buf_str(&body, env[i]);
    }
    rx_buf_fmt(&line, "test-exec %lld %zu", (long long)run, body.len);
    int64_t ex = -1;
    if (rx_request(ext, line.data, body.data, body.len, &fields, NULL) == 0) {
        if (!fields.data || !rx_positive_num(fields.data, INT64_MAX, &ex) || ex <= 0) {
            ex = -1;
            rx_test_local(ext, "syntax", "test-exec: a bad reply");
        }
    }
    rx_buf_free(&body);
    rx_buf_free(&line);
    rx_buf_free(&fields);
    return ex;
}

/* tests-resolve LEN + PARENT, test-run RUN PROFILE N LEN + ids: answered here */
static void rx_test_answer(rx_ext *ext, const rx_handlers *h, void *user, char **f, int n,
                           const unsigned char *body, size_t len) {
    rx_buf line = {NULL, 0, 0}, out = {NULL, 0, 0};
    int tests = rx_has(ext, "tests");
    if (tests && strcmp(f[1], "tests-resolve") == 0 && n == 3 && h->resolve_tests) {
        char *parent = (char *)malloc(len + 1);
        if (!parent) abort();
        if (len) memcpy(parent, body, len);
        parent[len] = 0;
        rx_test_items l;
        memset(&l, 0, sizeof l);
        int ok = strlen(parent) == len && h->resolve_tests(ext, parent, &l, user);
        if (ok && rx_test_items_finish(&l, parent, &out) == 0) {
            rx_buf_fmt(&line, "ok %s %zu", f[0], out.len);
            rx_write(line.data, out.data, out.len);
        } else if (ok) {
            rx_buf_fmt(&line, "error %s %s %s", f[0], l.fail, l.why);
            rx_write(line.data, NULL, 0);
        } else {
            rx_buf_fmt(&line, "error %s unknown no such request", f[0]);
            rx_write(line.data, NULL, 0);
        }
        rx_test_items_free(&l);
        free(parent);
    } else if (tests && strcmp(f[1], "test-run") == 0 && n == 6 && h->run_tests) {
        rx_test_run run;
        int64_t count = 0;
        if (!rx_positive_num(f[2], INT64_MAX, &run.run) || run.run <= 0 ||
            !rx_positive_num(f[4], 1000000, &count)) {
            rx_buf_fmt(&line, "error %s syntax a bad test-run", f[0]);
            rx_write(line.data, NULL, 0);
        } else {
            /* the ids, each ending in LF, in a copy of the body */
            char *ids = (char *)malloc(len + 1);
            const char **items = (const char **)calloc((size_t)count + 1, sizeof *items);
            if (!ids || !items) abort();
            if (len) memcpy(ids, body, len);
            ids[len] = 0;
            int k = 0;
            for (char *p = ids; *p && k < count;) {
                char *nl = strchr(p, '\n');
                if (nl) *nl = 0;
                items[k++] = p;
                if (!nl) break;
                p = nl + 1;
            }
            run.profile = f[3];
            run.nitems = k;
            run.items = items;
            const char *why = h->run_tests(ext, &run, user);
            if (why) rx_buf_fmt(&line, "error %s %s", f[0], why);
            else rx_buf_fmt(&line, "ok %s 0", f[0]);
            for (char *c = line.data; *c; c++)
                if (*c == '\n' || *c == '\r') *c = ' ';
            rx_write(line.data, NULL, 0);
            free(items);
            free(ids);
        }
    } else {
        rx_buf_fmt(&line, "error %s unknown no such request", f[0]);
        rx_write(line.data, NULL, 0);
    }
    rx_buf_free(&line);
    rx_buf_free(&out);
}

/* ---- terminals and tasks */

static const char *const rx_pat_fields[] = {"file", "line", "col", "endline", "endcol", "severity", "code", "message"};
static const char *const rx_sev_words[] = {"fatal error", "error", "fatal", "warning", "warn", "info", "note", "hint", "help"};
#define RX_TOK_LIT -1
#define RX_TOK_BLANK -2
#define RX_TOK_ANY 8
typedef struct { int kind; size_t lit, litlen; } rx_ptok;
typedef struct { rx_ptok t[32]; int n; char lit[512]; size_t nlit; } rx_pattern;

static const char *rx_pattern_compile(const char *p, size_t len, rx_pattern *cp) {
    unsigned seen = 0;
    cp->n = 0;
    cp->nlit = 0;
    if (len == 0 || len > 512) return "a pattern is 1 to 512 bytes";
    for (size_t i = 0; i < len;) {
        unsigned char c = (unsigned char)p[i];
        int kind = RX_TOK_LIT;
        if (c < 0x20 || c == 0x7f) return "a pattern is one line of text";
        if (c == ' ') {
            while (i < len && p[i] == ' ') i++;
            kind = RX_TOK_BLANK;
        } else if (c == '{' && i + 1 < len && p[i + 1] == '{') {
            i += 2;
        } else if (c == '{') {
            const char *end = i + 1 < len ? (const char *)memchr(p + i + 1, '}', len - i - 1) : NULL;
            if (!end) return "{ without }";
            size_t n = (size_t)(end - (p + i + 1));
            kind = -3;
            if (n == 1 && p[i + 1] == '_') kind = RX_TOK_ANY;
            for (int k = 0; k < 8; k++)
                if (strlen(rx_pat_fields[k]) == n && memcmp(rx_pat_fields[k], p + i + 1, n) == 0) kind = k;
            if (kind == -3) return "unknown field";
            if (kind != RX_TOK_ANY) {
                if (seen & (1u << kind)) return "a field twice";
                seen |= 1u << kind;
            }
            i += n + 2;
        } else if (c == '}') {
            if (i + 1 >= len || p[i + 1] != '}') return "a } alone (write }})";
            i += 2;
        } else {
            i++;
        }
        if (kind == RX_TOK_LIT && cp->n > 0 && cp->t[cp->n - 1].kind == RX_TOK_LIT) {
            cp->t[cp->n - 1].litlen++;
        } else {
            if (cp->n >= 32) return "more than 32 parts";
            cp->t[cp->n].kind = kind;
            cp->t[cp->n].lit = cp->nlit;
            cp->t[cp->n].litlen = kind == RX_TOK_LIT;
            cp->n++;
        }
        if (kind == RX_TOK_LIT) cp->lit[cp->nlit++] = (char)c;
    }
    return NULL;
}

const char *rx_check_pattern(const char *p, size_t len) {
    rx_pattern cp;
    return rx_pattern_compile(p, len, &cp);
}

typedef struct { const rx_pattern *cp; const char *line; size_t len; int steps; size_t (*caps)[2]; } rx_prun;

static int rx_pmatch(rx_prun *m, int tok, size_t pos) {
    if (++m->steps > 10000) return 0;
    if (tok == m->cp->n) return pos == m->len;
    const rx_ptok *t = &m->cp->t[tok];
    const char *l = m->line;
    if (t->kind == RX_TOK_LIT) {
        if (pos + t->litlen > m->len || memcmp(l + pos, m->cp->lit + t->lit, t->litlen) != 0) return 0;
        return rx_pmatch(m, tok + 1, pos + t->litlen);
    }
    if (t->kind == RX_TOK_BLANK) {
        for (size_t q = pos; q < m->len && (l[q] == ' ' || l[q] == '\t');) {
            if (rx_pmatch(m, tok + 1, ++q)) return 1;
            if (m->steps > 10000) return 0;
        }
        return 0;
    }
    if (t->kind == 5) {
        for (int w = 0; w < 9; w++) {
            size_t n = strlen(rx_sev_words[w]), k;
            if (pos + n > m->len) continue;
            for (k = 0; k < n; k++) {
                char c = l[pos + k];
                if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
                if (c != rx_sev_words[w][k]) break;
            }
            if (k < n) continue;
            m->caps[5][0] = pos, m->caps[5][1] = n;
            if (rx_pmatch(m, tok + 1, pos + n)) return 1;
        }
        return 0;
    }
    if (t->kind >= 1 && t->kind <= 4) {
        for (size_t n = 1; n <= 9 && pos + n <= m->len; n++) {
            if (l[pos + n - 1] < '0' || l[pos + n - 1] > '9') return 0;
            m->caps[t->kind][0] = pos, m->caps[t->kind][1] = n;
            if (rx_pmatch(m, tok + 1, pos + n)) return 1;
            if (m->steps > 10000) return 0;
        }
        return 0;
    }
    /* {file}, {code}, {message}: 1 or more bytes; {_}: 0 or more; the fewest first */
    if (t->kind == 0 && (pos >= m->len || l[pos] == ' ' || l[pos] == '\t')) return 0;
    for (size_t n = t->kind == RX_TOK_ANY ? 0 : 1; pos + n <= m->len; n++) {
        if (t->kind == 0 && n >= 3 && memcmp(l + pos + n - 3, "://", 3) == 0) return 0;
        if (t->kind != RX_TOK_ANY) m->caps[t->kind][0] = pos, m->caps[t->kind][1] = n;
        if (rx_pmatch(m, tok + 1, pos + n)) return 1;
        if (m->steps > 10000) return 0;
    }
    return 0;
}

int rx_match_pattern(const char *p, size_t plen, const char *line, size_t llen, size_t caps[8][2], unsigned *has) {
    rx_pattern cp;
    if (rx_pattern_compile(p, plen, &cp)) return 0;
    rx_prun m = {&cp, line, llen, 0, caps};
    memset(caps, 0, sizeof(size_t) * 16);
    if (has) {
        *has = 0;
        for (int i = 0; i < cp.n; i++)
            if (cp.t[i].kind >= 0 && cp.t[i].kind < 8) *has |= 1u << cp.t[i].kind;
    }
    return rx_pmatch(&m, 0, 0);
}

const char *rx_builtin_matcher(const char *name) {
    static const char *const names[] = {"go", "gcc", "rustc", "tsc", "msbuild"};
    static const char *const texts[] = {
        "columns bytes\n1 {file}:{line}:{col}: {message}\n1 vet: {file}:{line}:{col}: {message}\n1  {file}:{line}: {message}\n",
        "columns bytes\n1 {file}:{line}:{col}: {severity}: {message}\n1 {file}:{line}: {severity}: {message}\n",
        "1 {severity}[{code}]: {message}\n1 {severity}: {message}\n2  --> {file}:{line}:{col}\n",
        "1 {file}({line},{col}): {severity} {code}: {message}\n1 {file}:{line}:{col} - {severity} {code}: {message}\n",
        "1 {file}({line},{col}): {severity} {code}: {message}\n1 {file}({line},{col},{endline},{endcol}): {severity} {code}: {message}\n"};
    for (int i = 0; i < 5; i++)
        if (strcmp(name, names[i]) == 0) return texts[i];
    return NULL;
}

const char *rx_check_matcher(const char *text, size_t len) {
    static const char bad[] = "a matcher line is severity S, columns chars|bytes, N PATTERN or loop";
    int steps = 0, alts[4] = {0, 0, 0, 0};
    if (len > 4096) return "a matcher is at most 4096 bytes";
    for (size_t at = 0; at <= len;) {
        const char *nl = (const char *)memchr(text + at, '\n', len - at);
        size_t end = nl ? (size_t)(nl - text) : len, s = at, e = end;
        at = end + 1;
        while (s < e && (text[s] == ' ' || text[s] == '\t' || text[s] == '\r')) s++;
        while (e > s && (text[e - 1] == ' ' || text[e - 1] == '\t' || text[e - 1] == '\r')) e--;
        if (s == e) continue;
        size_t w = s;
        while (w < e && text[w] != ' ' && text[w] != '\t') w++;
        size_t wl = w - s;
        const char *rest = text + w + 1;
        size_t rl = w < e ? e - w - 1 : 0;
        if (wl == 4 && memcmp(text + s, "loop", 4) == 0) {
            if (w < e) return bad;
        } else if (w >= e) {
            return bad;
        } else if (wl == 8 && memcmp(text + s, "severity", 8) == 0) {
            if (!(rl == 5 && !memcmp(rest, "error", 5)) && !(rl == 7 && !memcmp(rest, "warning", 7)) &&
                !(rl == 4 && (!memcmp(rest, "info", 4) || !memcmp(rest, "hint", 4))))
                return bad;
        } else if (wl == 7 && memcmp(text + s, "columns", 7) == 0) {
            if (rl != 5 || (memcmp(rest, "bytes", 5) && memcmp(rest, "chars", 5))) return bad;
        } else if (wl == 1 && text[s] >= '1' && text[s] <= '4') {
            int n = text[s] - '0';
            if (n != steps && n != steps + 1) return "steps go 1, 2, 3, 4 in order";
            steps = n;
            if (alts[n - 1] >= 8) return "more than 8 patterns in a step";
            alts[n - 1]++;
            const char *why = rx_check_pattern(rest, rl);
            if (why) return why;
        } else {
            return bad;
        }
        if (!nl) break;
    }
    return steps ? NULL : "a matcher needs a step 1 pattern";
}

static int rx_task_id_ok(const char *s) {
    size_t n = s ? strlen(s) : 0;
    if (n == 0 || n > 64) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (i > 0 && (c == '_' || c == '.' || c == '-')))) return 0;
    }
    return 1;
}

/* one line of text without control characters (rhun also refuses bidi controls and invalid UTF-8) */
static int rx_line_ok(const char *s, size_t limit) {
    size_t n = s ? strlen(s) : 0;
    if (n == 0 || n > limit) return 0;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)s[i] < 0x20 || s[i] == 0x7f) return 0;
    return 1;
}

static void rx_record(rx_buf *b, const char *name, const char *args, const char *data, size_t len) {
    rx_buf_fmt(b, args ? "%s %s %zu\n" : "%s%s%zu\n", name, args ? args : " ", len);
    if (len) rx_buf_add(b, data, len);
}

static int rx_encode(const char *id, const char *label, const char *detail, const rx_program *p, const char *reveal,
                     int output, const rx_buf *more, int task, rx_buf *out, const char **why) {
    rx_buf cmd = {NULL, 0, 0}, extra = {NULL, 0, 0};
    const char *form = "shell";
    const char *err = NULL;
    if (!rx_line_ok(label, 128)) err = "label: 1 to 128 bytes of plain text";
    if (p->shell && p->shell[0]) {
        if (!rx_line_ok(p->shell, 4096)) err = "command: 1 to 4096 bytes of plain text";
        rx_buf_str(&cmd, p->shell);
    } else if (p->argv && p->argv[0]) {
        int n = 0;
        form = "argv";
        if (!p->argv[0][0]) err = "argv: 1 to 64 arguments, each ending with a NUL, the first not empty";
        for (const char *const *a = p->argv; *a; a++, n++) rx_buf_add(&cmd, *a, strlen(*a) + 1);
        if (n > 64 || cmd.len > 32768) err = "argv: 1 to 64 arguments, each ending with a NUL, the first not empty";
    } else if (task) {
        err = "command: 1 to 4096 bytes of plain text";
    }
    if (detail && detail[0]) {
        if (!rx_line_ok(detail, 200)) err = "detail: one line, at most 200 bytes";
        rx_record(&extra, "detail", NULL, detail, strlen(detail));
    }
    for (const char *const *v = p->env; v && *v; v++) {
        const char *eq = strchr(*v, '=');
        char name[72];
        size_t nl = eq ? (size_t)(eq - *v) : 0;
        if (!eq || nl == 0 || nl > 64 || strchr(eq, '\n')) {
            err = "env: NAME is [A-Za-z_][A-Za-z0-9_]*, the value one line";
            continue;
        }
        memcpy(name, *v, nl);
        name[nl] = 0;
        rx_record(&extra, "env", name, eq + 1, strlen(eq + 1));
    }
    for (const char *const *v = p->unset; v && *v; v++) rx_record(&extra, "unset", *v, NULL, 0);
    if (reveal) {
        if (strcmp(reveal, "show") && strcmp(reveal, "focus") && strcmp(reveal, "silent")) err = "reveal: show, focus or silent";
        rx_record(&extra, "reveal", reveal, NULL, 0);
    }
    if (output) rx_record(&extra, "output", NULL, NULL, 0);
    if (more) rx_buf_add(&extra, more->data, more->len);
    const char *cwd = p->cwd ? p->cwd : "";
    if (strlen(cwd) > 4096 || strchr(cwd, '\n')) err = "cwd: at most 4096 bytes, no NUL or LF";
    if (!err) {
        rx_buf_fmt(out, "%s %s %zu %zu %zu %zu\n", id, form, strlen(label), cmd.len, strlen(cwd), extra.len);
        rx_buf_str(out, label);
        rx_buf_add(out, cmd.data, cmd.len);
        rx_buf_str(out, cwd);
        rx_buf_add(out, extra.data, extra.len);
    }
    rx_buf_free(&cmd);
    rx_buf_free(&extra);
    if (why) *why = err;
    return err ? -1 : 0;
}

int rx_task_encode(const rx_task *t, rx_buf *out, const char **why) {
    rx_buf more = {NULL, 0, 0};
    const char *err = NULL;
    int n = 0;
    if (!rx_task_id_ok(t->id)) err = "id: 1 to 64 bytes of a-z 0-9 _ . -, from a letter or digit";
    if (t->group) {
        if (strcmp(t->group, "build") && strcmp(t->group, "test")) err = "group: build or test";
        rx_record(&more, "group", t->group, NULL, 0);
    }
    if (t->is_default) rx_record(&more, "default", NULL, NULL, 0);
    if (t->depends && t->depends[0]) {
        rx_buf ids = {NULL, 0, 0};
        int k = 0;
        for (const char *const *d = t->depends; *d; d++, k++) {
            if (!rx_task_id_ok(*d) || k >= 16) err = "depends: 1 to 16 task ids";
            if (k) rx_buf_str(&ids, " ");
            rx_buf_str(&ids, *d);
        }
        rx_record(&more, "depends", t->parallel ? "parallel" : "sequence", ids.data, ids.len);
        rx_buf_free(&ids);
    }
    for (const char *const *m = t->builtin_matchers; m && *m; m++, n++) {
        if (!rx_builtin_matcher(*m)) err = "matcher: go, gcc, rustc, tsc or msbuild";
        rx_record(&more, "matcher", *m, NULL, 0);
    }
    for (const char *const *m = t->matcher_texts; m && *m; m++, n++) {
        const char *w = rx_check_matcher(*m, strlen(*m));
        if (w) err = w;
        rx_record(&more, "matcher", "-", *m, strlen(*m));
    }
    if (n > 4) err = "too many: 64 env, 4 matchers, 8 patterns";
    for (int r = 0; r < 2; r++) {
        const char *const *list = r ? t->busy : t->ready;
        rx_buf pats = {NULL, 0, 0};
        int k = 0;
        for (; list && *list; list++, k++) {
            const char *w = rx_check_pattern(*list, strlen(*list));
            if (w) err = w;
            if (k) rx_buf_str(&pats, "\n");
            rx_buf_str(&pats, *list);
        }
        if (k > 8) err = "too many: 64 env, 4 matchers, 8 patterns";
        if (k) rx_record(&more, r ? "busy" : "ready", NULL, pats.data, pats.len);
        rx_buf_free(&pats);
    }
    if (t->busy && t->busy[0] && !(t->ready && t->ready[0])) err = "busy needs ready";
    int rc = err ? -1 : rx_encode(t->id, t->label, t->detail, &t->program, t->reveal, t->output, &more, 1, out, &err);
    rx_buf_free(&more);
    if (why) *why = err;
    return err ? -1 : rc;
}

int rx_terminal_encode(const rx_terminal_spec *s, rx_buf *out, const char **why) {
    return rx_encode("-", s->label, s->detail, &s->program, s->reveal, s->output, NULL, 0, out, why);
}

static int rx_term_agreed(rx_ext *ext, const char *feature) {
    if (rx_has(ext, feature)) return 1;
    snprintf(ext->error, sizeof ext->error, "unknown %s was not agreed", feature);
    return 0;
}

static rx_term rx_term_request(rx_ext *ext, const char *verb_args, const void *body, size_t len) {
    rx_buf f = {NULL, 0, 0};
    rx_term t = 0;
    if (rx_request(ext, verb_args, body, len, &f, NULL) == 0) t = f.data ? strtoull(f.data, NULL, 10) : 0;
    rx_buf_free(&f);
    return t;
}

int rx_set_tasks(rx_ext *ext, const rx_task *tasks, size_t n) {
    if (!rx_term_agreed(ext, "tasks")) return -1;
    rx_buf body = {NULL, 0, 0};
    rx_buf_fmt(&body, "%zu\n", n);
    for (size_t i = 0; i < n; i++) {
        const char *why;
        if (rx_task_encode(&tasks[i], &body, &why) < 0) {
            snprintf(ext->error, sizeof ext->error, "syntax task %zu: %s", i + 1, why);
            rx_buf_free(&body);
            return -1;
        }
    }
    char line[48];
    snprintf(line, sizeof line, "tasks-set %zu", body.len);
    int r = rx_request(ext, line, body.data, body.len, NULL, NULL);
    rx_buf_free(&body);
    return r;
}

rx_term rx_start_task(rx_ext *ext, const char *id) {
    if (!rx_term_agreed(ext, "tasks")) return 0;
    if (!rx_task_id_ok(id)) {
        snprintf(ext->error, sizeof ext->error, "syntax a task id");
        return 0;
    }
    char line[96];
    snprintf(line, sizeof line, "task-start %s", id);
    return rx_term_request(ext, line, NULL, 0);
}

static rx_term rx_start_def(rx_ext *ext, const char *verb, const rx_buf *def) {
    char line[48];
    snprintf(line, sizeof line, "%s %zu", verb, def->len);
    return rx_term_request(ext, line, def->data, def->len);
}

rx_term rx_run_task(rx_ext *ext, const rx_task *t) {
    if (!rx_term_agreed(ext, "tasks")) return 0;
    rx_buf b = {NULL, 0, 0};
    const char *why;
    rx_term r = 0;
    if (rx_task_encode(t, &b, &why) < 0) snprintf(ext->error, sizeof ext->error, "syntax definition: %s", why);
    else r = rx_start_def(ext, "task-run", &b);
    rx_buf_free(&b);
    return r;
}

rx_term rx_open_terminal(rx_ext *ext, const rx_terminal_spec *s) {
    if (!rx_term_agreed(ext, "terminals")) return 0;
    rx_buf b = {NULL, 0, 0};
    const char *why;
    rx_term r = 0;
    if (rx_terminal_encode(s, &b, &why) < 0) snprintf(ext->error, sizeof ext->error, "syntax definition: %s", why);
    else r = rx_start_def(ext, "terminal-open", &b);
    rx_buf_free(&b);
    return r;
}

static int rx_term_verb(rx_ext *ext, const char *verb, rx_term t, const char *more) {
    if (!rx_term_agreed(ext, "terminals")) return -1;
    char line[96];
    snprintf(line, sizeof line, "%s %llu%s", verb, (unsigned long long)t, more);
    return rx_request(ext, line, NULL, 0, NULL, NULL);
}

int rx_stop_terminal(rx_ext *ext, rx_term t) { return rx_term_verb(ext, "terminal-stop", t, ""); }
int rx_restart_terminal(rx_ext *ext, rx_term t) { return rx_term_verb(ext, "terminal-restart", t, ""); }
int rx_close_terminal(rx_ext *ext, rx_term t) { return rx_term_verb(ext, "terminal-close", t, ""); }
int rx_show_terminal(rx_ext *ext, rx_term t, int focus) {
    return rx_term_verb(ext, "terminal-show", t, focus ? " focus" : " show");
}

int rx_terminal_input(rx_ext *ext, rx_term t, const void *bytes, size_t len) {
    if (!rx_term_agreed(ext, "terminals")) return -1;
    if (len > 65536) {
        snprintf(ext->error, sizeof ext->error, "toobig 65536 bytes");
        return -1;
    }
    char line[96];
    snprintf(line, sizeof line, "terminal-input %llu %zu", (unsigned long long)t, len);
    return rx_request(ext, line, bytes, len, NULL, NULL);
}

int rx_terminal_screen(rx_ext *ext, rx_term t, rx_buf *body) {
    if (!rx_term_agreed(ext, "terminals")) return -1;
    char line[64];
    snprintf(line, sizeof line, "terminal-screen %llu", (unsigned long long)t);
    return rx_request(ext, line, NULL, 0, NULL, body);
}

static int rx_unum(const char *s, int64_t *out) {
    if (!s || !*s || strlen(s) > 18) return 0;
    for (const char *p = s; *p; p++)
        if (*p < '0' || *p > '9') return 0;
    *out = atoll(s);
    return 1;
}

int rx_parse_terminal_event(const rx_event *ev, rx_terminal_event *out) {
    int64_t term = 0;
    memset(out, 0, sizeof *out);
    if (ev->argc < 1 || !rx_unum(ev->argv[0], &term) || term == 0) return 0;
    out->term = (rx_term)term;
    const char *n = ev->name;
    if (strcmp(n, "terminal-started") == 0 && ev->argc == 3) {
        if (!rx_unum(ev->argv[1], &out->pid) || strlen(ev->argv[2]) > 64) return 0;
        if (strcmp(ev->argv[2], "-")) snprintf(out->task, sizeof out->task, "%s", ev->argv[2]);
        snprintf(out->kind, sizeof out->kind, "started");
        return 1;
    }
    if (strcmp(n, "terminal-ended") == 0 && ev->argc == 5) {
        out->code = -1;
        if (strcmp(ev->argv[2], "-") && !rx_unum(ev->argv[2], &out->code)) return 0;
        if (strlen(ev->argv[1]) > 7 || !rx_unum(ev->argv[3], &out->errors) || !rx_unum(ev->argv[4], &out->warnings)) return 0;
        snprintf(out->reason, sizeof out->reason, "%s", ev->argv[1]);
        snprintf(out->kind, sizeof out->kind, "ended");
        return 1;
    }
    if (strcmp(n, "terminal-output") == 0 && ev->argc == 3) {
        if (!rx_unum(ev->argv[1], &out->dropped)) return 0;
        out->lines = ev->body, out->lines_len = ev->body_len;
        snprintf(out->kind, sizeof out->kind, "output");
        return 1;
    }
    if (strcmp(n, "terminal-closed") == 0 && ev->argc == 1) {
        snprintf(out->kind, sizeof out->kind, "closed");
        return 1;
    }
    if (strcmp(n, "task-ready") == 0 && ev->argc == 3) {
        if (!rx_unum(ev->argv[1], &out->errors) || !rx_unum(ev->argv[2], &out->warnings)) return 0;
        snprintf(out->kind, sizeof out->kind, "ready");
        return 1;
    }
    if (strcmp(n, "task-busy") == 0 && ev->argc == 1) {
        snprintf(out->kind, sizeof out->kind, "busy");
        return 1;
    }
    return 0;
}

/* ---- the loop */

/* fields f[from] to f[to - 1] are numbers: digits only */
static int rx_nums(char **f, int from, int to) {
    for (int i = from; i < to; i++) {
        const char *s = f[i];
        if (!*s) return 0;
        for (; *s; s++)
            if (*s < '0' || *s > '9') return 0;
    }
    return 1;
}

/* f[at] to f[at + 3] are TAB_WIDTH SPACES TRIM FINAL (partial-format): a tab width of at least 1,
 * then yes or no */
static int rx_format_options(char **f, int at, int *opt) {
    if (!rx_nums(f, at, at + 1) || atoi(f[at]) < 1) return 0;
    opt[0] = atoi(f[at]);
    for (int i = 1; i < 4; i++) {
        if (strcmp(f[at + i], "yes") != 0 && strcmp(f[at + i], "no") != 0) return 0;
        opt[i] = f[at + i][0] == 'y';
    }
    return 1;
}

/* ---- protocol 1.1 language features: hover-query, selection-ranges, hierarchy,
 * workspace-symbols, document-links, file-diagnostics */

#define RX_HOVER_MAX ((size_t)64 << 10)
#define RX_SELECTION_MAX 64
#define RX_SELECTION_BODY ((size_t)64 << 10)
#define RX_ROOTS 64
#define RX_ROOTS_BODY ((size_t)256 << 10)
#define RX_HIERARCHY 1000
#define RX_HIERARCHY_BODY ((size_t)1 << 20)
#define RX_HIERARCHY_DATA ((size_t)16 << 10)
#define RX_CALL_SITES 1000
#define RX_DIAGNOSTICS 2000
#define RX_DIAGNOSTICS_BODY ((size_t)4 << 20)
#define RX_DIAGNOSTIC_MESSAGE 4000
#define RX_RELATED 16

/* a before b */
static int rx_before(rx_pos a, rx_pos b) { return a.line < b.line || (a.line == b.line && a.col < b.col); }
static int rx_well_formed(rx_range r) { return !rx_before(r.end, r.start); }
static int rx_contains(rx_range a, rx_range b) { return !rx_before(b.start, a.start) && !rx_before(a.end, b.end); }
static int rx_same_range(rx_range a, rx_range b) {
    return a.start.line == b.start.line && a.start.col == b.start.col && a.end.line == b.end.line &&
           a.end.col == b.end.col;
}
static void rx_add_range(rx_buf *b, rx_range r) {
    rx_buf_fmt(b, "%d %d %d %d", rx_nn(r.start.line), rx_nn(r.start.col), rx_nn(r.end.line), rx_nn(r.end.col));
}

void rx_hover_answer(rx_buf *out, rx_range r, const char *text, size_t len) {
    if (!text || !len) return;
    if (!rx_well_formed(r)) memset(&r, 0, sizeof r);
    size_t at = out->len;
    rx_add_range(out, r);
    rx_buf_add(out, "\n", 1);
    size_t head = out->len - at;
    rx_buf_add(out, text, rx_cut(text, len, RX_HOVER_MAX - head));
}

void rx_selection_group(rx_selections *s, const rx_range *ranges, int n) {
    if (s->n >= RX_SELECTION_MAX) return;
    int m = 0;
    rx_range last;
    memset(&last, 0, sizeof last);
    for (int i = 0; i < n && m < RX_SELECTION_MAX; i++) {
        rx_range r = ranges[i];
        if (!rx_well_formed(r)) break;
        if (m && rx_same_range(r, last)) continue;
        if (m && !rx_contains(r, last)) break;
        rx_buf_add(&s->ranges, &r, sizeof r);
        last = r;
        m++;
    }
    s->counts[s->n++] = m;
}

/* the groups with at most m ranges each */
static void rx_selections_encode(const rx_selections *s, int groups, int m, rx_buf *out) {
    const rx_range *all = (const rx_range *)(const void *)s->ranges.data;
    size_t at = 0;
    for (int g = 0; g < groups; g++) {
        int c = s->counts[g];
        if (c == 0) {
            rx_range r = {s->at[g], s->at[g]};
            rx_buf_str(out, "1\n");
            rx_add_range(out, r);
            rx_buf_add(out, "\n", 1);
        } else {
            int k = c < m ? c : m;
            rx_buf_fmt(out, "%d\n", k);
            for (int i = 0; i < k; i++) {
                rx_add_range(out, all[at + (size_t)i]);
                rx_buf_add(out, "\n", 1);
            }
        }
        at += (size_t)c;
    }
}

void rx_selections_finish(rx_selections *s, rx_buf *out) {
    int groups = s->n < s->npos ? s->n : s->npos;
    while (groups > 0 && s->counts[groups - 1] == 0) groups--;
    int lo = 1, hi = RX_SELECTION_MAX;
    while (lo < hi) {
        int m = (lo + hi + 1) / 2;
        rx_buf t = {NULL, 0, 0};
        rx_selections_encode(s, groups, m, &t);
        if (t.len <= RX_SELECTION_BODY) lo = m;
        else hi = m - 1;
        rx_buf_free(&t);
    }
    rx_selections_encode(s, groups, lo, out);
    rx_buf_free(&s->ranges);
    s->n = 0;
}

/* an item's header (without its line end) and bytes into b, or 0 when rhun would refuse it */
static int rx_item_record(const rx_hierarchy_item *it, rx_buf *b) {
    char tmp[32];
    const char *name = it->name ? it->name : "", *detail = it->detail ? it->detail : "";
    const char *t = rx_target(it->path, it->doc, tmp, sizeof tmp);
    size_t nl = rx_cut(name, strlen(name), RX_TEXT_MAX), dl = rx_cut(detail, strlen(detail), RX_TEXT_MAX);
    size_t tl = strlen(t);
    if (!nl || tl > RX_PATH_MAX || it->data_len > RX_HIERARCHY_DATA || (it->data_len && !it->data) ||
        !rx_well_formed(it->range))
        return 0;
    rx_range sel = rx_well_formed(it->selection) && rx_contains(it->range, it->selection) ? it->selection : it->range;
    rx_add_word(b, it->kind, "function");
    rx_buf_add(b, " ", 1);
    rx_add_range(b, it->range);
    rx_buf_add(b, " ", 1);
    rx_add_range(b, sel);
    rx_buf_fmt(b, " %zu %zu %zu %zu", tl, nl, dl, it->data_len);
    return 1;
}

static void rx_item_bytes(const rx_hierarchy_item *it, rx_buf *b) {
    char tmp[32];
    const char *name = it->name ? it->name : "", *detail = it->detail ? it->detail : "";
    const char *t = rx_target(it->path, it->doc, tmp, sizeof tmp);
    rx_buf_add(b, t, strlen(t));
    rx_add_line(b, name, rx_cut(name, strlen(name), RX_TEXT_MAX));
    rx_add_line(b, detail, rx_cut(detail, strlen(detail), RX_TEXT_MAX));
    rx_buf_add(b, it->data, it->data_len);
}

void rx_hierarchy_add(rx_hierarchy *l, const rx_hierarchy_item *it) {
    int max = l->roots ? RX_ROOTS : RX_HIERARCHY;
    size_t mark = l->recs.len;
    if (l->n >= max) l->full = 1;
    if (l->full || !it) return;
    if (!rx_item_record(it, &l->recs)) {
        l->recs.len = mark;
        if (l->recs.data) l->recs.data[mark] = 0;
        return;
    }
    rx_buf_add(&l->recs, "\n", 1);
    rx_item_bytes(it, &l->recs);
    rx_keep(&l->recs, mark, &l->n, &l->full, l->roots ? RX_ROOTS_BODY : RX_HIERARCHY_BODY);
}

void rx_call_add(rx_calls *l, const rx_hierarchy_item *it, const rx_range *sites, int nsites) {
    size_t mark = l->recs.len;
    if (l->n >= RX_HIERARCHY) l->full = 1;
    if (l->full || !it) return;
    rx_buf sb = {NULL, 0, 0};
    int r = 0;
    for (int i = 0; i < nsites && r < RX_CALL_SITES; i++) {
        if (!rx_well_formed(sites[i])) continue;
        rx_add_range(&sb, sites[i]);
        rx_buf_add(&sb, "\n", 1);
        r++;
    }
    if (r && rx_item_record(it, &l->recs)) {
        rx_buf_fmt(&l->recs, " %d\n", r);
        rx_item_bytes(it, &l->recs);
        rx_buf_add(&l->recs, sb.data, sb.len);
        rx_keep(&l->recs, mark, &l->n, &l->full, RX_HIERARCHY_BODY);
    } else {
        l->recs.len = mark;
        if (l->recs.data) l->recs.data[mark] = 0;
    }
    rx_buf_free(&sb);
}

void rx_workspace_symbol(rx_workspace_symbols *l, const char *path, rx_doc doc, rx_range r, const char *kind,
                         const char *name, const char *detail) {
    if (l->n >= RX_RECORDS) l->full = 1;
    if (l->full) return;
    char tmp[32];
    const char *t = rx_target(path, doc, tmp, sizeof tmp);
    if (!name) name = "";
    if (!detail) detail = "";
    size_t tl = strlen(t), nl = rx_cut(name, strlen(name), RX_TEXT_MAX), dl = rx_cut(detail, strlen(detail), RX_TEXT_MAX);
    size_t mark = l->recs.len;
    if (!nl || tl > RX_PATH_MAX) return;
    rx_add_range(&l->recs, r);
    rx_buf_add(&l->recs, " ", 1);
    rx_add_word(&l->recs, kind, "function");
    rx_buf_fmt(&l->recs, " %zu %zu %zu\n", tl, nl, dl);
    rx_buf_add(&l->recs, t, tl);
    rx_add_line(&l->recs, name, nl);
    rx_add_line(&l->recs, detail, dl);
    rx_keep(&l->recs, mark, &l->n, &l->full, RX_RECORDS_BODY);
}

/* an http or https URL of at most max bytes without white space or control characters */
static int rx_url(const char *s, size_t max) {
    size_t n = strlen(s), p = 0;
    char low[9] = {0};
    for (size_t i = 0; i < 8 && i < n; i++) low[i] = (char)(s[i] >= 'A' && s[i] <= 'Z' ? s[i] + 32 : s[i]);
    if (memcmp(low, "https://", 8) == 0) p = 8;
    else if (memcmp(low, "http://", 7) == 0) p = 7;
    if (!p || n == p || n > max) return 0;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)s[i] <= ' ' || s[i] == 0x7f) return 0;
    return 1;
}

/* an absolute path: /..., a drive (C:\ or C:/) or \\server\share */
static int rx_abs_path(const char *p) {
    if (p[0] == '/' || (p[0] == '\\' && p[1] == '\\')) return 1;
    return ((p[0] >= 'a' && p[0] <= 'z') || (p[0] >= 'A' && p[0] <= 'Z')) && p[1] == ':' &&
           (p[2] == '\\' || p[2] == '/');
}

/* a file rhun takes: an absolute path of at most 4096 bytes on one line, or (path NULL or "") an
 * open document */
static int rx_valid_target(const char *path, rx_doc doc) {
    if (!path || !*path) return doc > 0;
    return strlen(path) <= RX_PATH_MAX && rx_abs_path(path) && !strpbrk(path, "\r\n");
}

int rx_link_target_valid(const char *t) {
    if (!t || !*t) return 0;
    if (rx_url(t, RX_PATH_MAX)) return 1;
    if (t[0] == '#') {
        size_t n = strlen(t + 1);
        if (n < 1 || n > 10) return 0;
        for (size_t i = 1; i <= n; i++)
            if (t[i] < '0' || t[i] > '9') return 0;
        return atoll(t + 1) > 0;
    }
    return rx_abs_path(t) && strlen(t) <= RX_PATH_MAX + 22 && !strpbrk(t, "\r\n");
}

void rx_document_link(rx_links *l, rx_range r, const char *target, const char *tip) {
    if (l->n >= RX_RECORDS) l->full = 1;
    if (l->full || !rx_link_target_valid(target) || !rx_well_formed(r)) return;
    if (!tip) tip = "";
    size_t tl = strlen(target), pl = rx_cut(tip, strlen(tip), RX_TEXT_MAX), mark = l->recs.len;
    rx_add_range(&l->recs, r);
    rx_buf_fmt(&l->recs, " %zu %zu\n", tl, pl);
    rx_buf_add(&l->recs, target, tl);
    rx_add_line(&l->recs, tip, pl);
    rx_keep(&l->recs, mark, &l->n, &l->full, RX_RECORDS_BODY);
}

void rx_hierarchy_finish(rx_hierarchy *l, rx_buf *out) { rx_records_finish(&l->recs, &l->n, &l->full, out); }
void rx_calls_finish(rx_calls *l, rx_buf *out) { rx_records_finish(&l->recs, &l->n, &l->full, out); }
void rx_workspace_symbols_finish(rx_workspace_symbols *l, rx_buf *out) {
    rx_records_finish(&l->recs, &l->n, &l->full, out);
}
void rx_links_finish(rx_links *l, rx_buf *out) { rx_records_finish(&l->recs, &l->n, &l->full, out); }

/* ---- editor presentation (protocol 1.1) ---- */

static int rx_deco_name_ok(const char *s) {
    size_t n = s ? strlen(s) : 0;
    if (!n || n > 40) return 0;
    for (size_t i = 0; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '_' || s[i] == '-')) return 0;
    return 1;
}

int rx_decoration_type(rx_ext *ext, const char *name, const char *body) {
    if (!rx_deco_name_ok(name)) return rx_fail(ext, "syntax a decoration type name is 1 to 40 of a-z 0-9 _ -");
    if (!body) body = "";
    rx_buf line = {NULL, 0, 0};
    rx_buf_fmt(&line, "decoration-type %s %zu", name, strlen(body));
    int r = rx_request(ext, line.data, body, strlen(body), NULL, NULL);
    rx_buf_free(&line);
    return r;
}

int rx_decorate(rx_ext *ext, rx_doc doc, int64_t version, const char *name, int from, int to,
                const rx_decoration *ranges, int n) {
    if (!rx_deco_name_ok(name)) return rx_fail(ext, "syntax a decoration type name is 1 to 40 of a-z 0-9 _ -");
    rx_buf body = {NULL, 0, 0}, line = {NULL, 0, 0};
    int count = 0;
    for (int i = 0; i < n && count < 20000; i++) {
        const rx_decoration *d = &ranges[i];
        if (!rx_well_formed(d->r)) continue;
        const char *b = d->before ? d->before : "", *a = d->after ? d->after : "", *t = d->tip ? d->tip : "";
        size_t bl = rx_cut(b, strlen(b), 200), al = rx_cut(a, strlen(a), 200), tl = rx_cut(t, strlen(t), 1000);
        rx_buf extra = {NULL, 0, 0};
        if (d->command && rx_command_ref(&extra, d->command, d->args, d->args_len) < 0) extra.len = 0;
        rx_add_range(&body, d->r);
        rx_buf_fmt(&body, " %zu %zu %zu %zu\n", bl, al, tl, extra.len);
        rx_add_line(&body, b, bl);
        rx_add_line(&body, a, al);
        rx_buf_add(&body, t, tl);
        rx_buf_add(&body, extra.data, extra.len);
        rx_buf_free(&extra);
        count++;
    }
    rx_buf_fmt(&line, "decorate %lld %lld %s %d %d %d %zu", (long long)doc, (long long)version, name, from, to, count,
               body.len);
    int r = rx_request(ext, line.data, body.data, body.len, NULL, NULL);
    rx_buf_free(&line);
    rx_buf_free(&body);
    return r;
}

int rx_semantic_legend(rx_ext *ext, const char *name, const char *types, const char *modifiers) {
    rx_buf body = {NULL, 0, 0}, line = {NULL, 0, 0};
    rx_buf_fmt(&body, "%s\n%s", types ? types : "", modifiers ? modifiers : "");
    rx_buf_fmt(&line, "semantic-legend %s %zu", name ? name : "", body.len);
    int r = rx_request(ext, line.data, body.data, body.len, NULL, NULL);
    rx_buf_free(&line);
    rx_buf_free(&body);
    return r;
}

int rx_visible(const rx_event *ev, rx_doc *doc, int *first, int *last) {
    if (!ev || strcmp(ev->name, "visible") != 0 || ev->argc != 3) return 0;
    *doc = atoll(ev->argv[0]);
    *first = atoi(ev->argv[1]);
    *last = atoi(ev->argv[2]);
    return 1;
}

const char *rx_tagged_severity(rx_ext *ext, const char *severity, int unnecessary, int deprecated, char out[48]) {
    snprintf(out, 48, "%s%s%s", severity ? severity : "error",
             rx_has(ext, "diagnostic-tags") && unnecessary ? "+unnecessary" : "",
             rx_has(ext, "diagnostic-tags") && deprecated ? "+deprecated" : "");
    return out;
}

static int rx_pres_room(rx_pres *l, int max) {
    if (l->n >= max) l->full = 1;
    return !l->full;
}

void rx_folding_range(rx_pres *l, int start, int end, const char *kind, const char *text) {
    if (!rx_pres_room(l, 50000) || start < 1 || end <= start) return;
    if (!kind || (strcmp(kind, "region") && strcmp(kind, "comment") && strcmp(kind, "imports"))) kind = "-";
    if (!text) text = "";
    size_t tl = rx_cut(text, strlen(text), 80), mark = l->recs.len;
    rx_buf_fmt(&l->recs, "%d %d %s %zu\n", start, end, kind, tl);
    rx_add_line(&l->recs, text, tl);
    rx_keep(&l->recs, mark, &l->n, &l->full, 4u << 20);
}

void rx_inlay_hint(rx_pres *l, rx_pos at, int before, const rx_hint_part *parts, int nparts,
                   const rx_range *edit_ranges, const char *const *edit_texts, int nedits) {
    if (!rx_pres_room(l, 5000) || at.line < 1 || at.col < 1) return;
    rx_buf ps = {NULL, 0, 0}, ex = {NULL, 0, 0};
    size_t total = 0;
    int np = 0;
    for (int i = 0; i < nparts && np < 8; i++) {
        const char *label = parts[i].label ? parts[i].label : "";
        size_t ll = rx_cut(label, strlen(label), 120 - total);
        if (!ll) continue;
        total += ll;
        const char *tip = parts[i].tip ? parts[i].tip : "";
        size_t tl = rx_cut(tip, strlen(tip), 4000);
        rx_buf pe = {NULL, 0, 0};
        if (parts[i].command && rx_command_ref(&pe, parts[i].command, parts[i].args, parts[i].args_len) < 0) pe.len = 0;
        if (parts[i].has_goto) {
            const char *p = parts[i].goto_path ? parts[i].goto_path : "";
            rx_buf_fmt(&pe, "goto %d %d %zu\n", parts[i].goto_at.line, parts[i].goto_at.col, strlen(p));
            rx_add_line(&pe, p, strlen(p));
        }
        rx_buf_fmt(&ps, "%zu %zu %zu\n", ll, tl, pe.len);
        rx_add_line(&ps, label, ll);
        rx_buf_add(&ps, tip, tl);
        rx_buf_add(&ps, pe.data, pe.len);
        rx_buf_free(&pe);
        np++;
    }
    for (int i = 0; i < nedits && i < 16 && np; i++) {
        if (!rx_well_formed(edit_ranges[i])) break;
        const char *t = edit_texts[i] ? edit_texts[i] : "";
        rx_buf_add(&ex, "edit ", 5);
        rx_add_range(&ex, edit_ranges[i]);
        rx_buf_fmt(&ex, " %zu\n", strlen(t));
        rx_buf_add(&ex, t, strlen(t));
    }
    if (np) {
        size_t mark = l->recs.len;
        rx_buf_fmt(&l->recs, "%d %d %s %d %zu\n", at.line, at.col, before ? "before" : "after", np, ex.len);
        rx_buf_add(&l->recs, ps.data, ps.len);
        rx_buf_add(&l->recs, ex.data, ex.len);
        rx_keep(&l->recs, mark, &l->n, &l->full, 4u << 20);
    }
    rx_buf_free(&ps);
    rx_buf_free(&ex);
}

void rx_code_lens(rx_pres *l, int line, const char *title, const char *tip, const char *command, const void *args,
                  size_t args_len) {
    if (!rx_pres_room(l, 10000) || line < 1) return;
    if (!title) title = "";
    size_t tl = rx_cut(title, strlen(title), 200), mark = l->recs.len;
    rx_buf ex = {NULL, 0, 0};
    if (command && rx_command_ref(&ex, command, args, args_len) < 0) ex.len = 0;
    if (tip && *tip) {
        size_t pl = rx_cut(tip, strlen(tip), 1000);
        rx_buf_fmt(&ex, "tip %zu\n", pl);
        rx_buf_add(&ex, tip, pl);
    }
    rx_buf_fmt(&l->recs, "%d %zu %zu\n", line, tl, ex.len);
    rx_add_line(&l->recs, title, tl);
    rx_buf_add(&l->recs, ex.data, ex.len);
    rx_buf_free(&ex);
    rx_keep(&l->recs, mark, &l->n, &l->full, 4u << 20);
}

void rx_highlight(rx_pres *l, rx_range r, const char *kind) {
    if (!rx_pres_room(l, 5000) || !rx_well_formed(r) || r.start.line < 1 || r.start.col < 1) return;
    if (!kind || (strcmp(kind, "read") && strcmp(kind, "write"))) kind = "text";
    size_t mark = l->recs.len;
    rx_add_range(&l->recs, r);
    rx_buf_fmt(&l->recs, " %s\n", kind);
    rx_keep(&l->recs, mark, &l->n, &l->full, 1u << 20);
}

void rx_document_color(rx_pres *l, rx_range r, uint32_t rgba) {
    if (!rx_pres_room(l, 10000) || !rx_well_formed(r) || r.start.line < 1 || r.start.col < 1) return;
    size_t mark = l->recs.len;
    rx_add_range(&l->recs, r);
    rx_buf_fmt(&l->recs, " %08x\n", (unsigned)rgba);
    rx_keep(&l->recs, mark, &l->n, &l->full, 1u << 20);
}

void rx_inline_item(rx_pres *l, rx_range r, const char *text, const char *command, const void *args,
                    size_t args_len) {
    rx_inline_item_edits(l, r, text, command, args, args_len, NULL, NULL, 0);
}

void rx_inline_item_edits(rx_pres *l, rx_range r, const char *text, const char *command, const void *args,
                          size_t args_len, const rx_range *edit_ranges, const char *const *edit_texts, int nedits) {
    if (!rx_pres_room(l, 8) || !rx_well_formed(r) || r.start.line < 1 || !text || !*text) return;
    size_t tl = strlen(text);
    if (tl > (64u << 10)) return;
    rx_buf ex = {NULL, 0, 0};
    if (command && rx_command_ref(&ex, command, args, args_len) < 0) ex.len = 0;
    for (int i = 0; i < nedits && i < 16; i++) {
        if (!rx_well_formed(edit_ranges[i])) break;
        const char *t = edit_texts[i] ? edit_texts[i] : "";
        rx_buf_add(&ex, "edit ", 5);
        rx_add_range(&ex, edit_ranges[i]);
        rx_buf_fmt(&ex, " %zu\n", strlen(t));
        rx_buf_add(&ex, t, strlen(t));
    }
    size_t mark = l->recs.len;
    rx_add_range(&l->recs, r);
    rx_buf_fmt(&l->recs, " %zu %zu\n", tl, ex.len);
    rx_buf_add(&l->recs, text, tl);
    rx_buf_add(&l->recs, ex.data, ex.len);
    rx_buf_free(&ex);
    rx_keep(&l->recs, mark, &l->n, &l->full, 4u << 20);
}

void rx_pres_finish(rx_pres *l, rx_buf *out) { rx_records_finish(&l->recs, &l->n, &l->full, out); }

void rx_semantic_full(rx_buf *out, const char *result, const char *legend, const uint64_t *data, size_t n, int range) {
    if (n % 5 || n / 5 > 200000) return;
    rx_buf_fmt(out, "%s %s %s %zu\n", result && *result ? result : "-", legend ? legend : "", range ? "range" : "full",
               n / 5);
    for (size_t i = 0; i < n; i++) rx_buf_fmt(out, i ? " %llu" : "%llu", (unsigned long long)data[i]);
}

void rx_semantic_delta(rx_buf *out, const char *result, const char *legend, int nedits) {
    rx_buf_fmt(out, "%s %s delta %d\n", result && *result ? result : "-", legend ? legend : "", nedits);
}

void rx_semantic_edit(rx_buf *out, uint32_t start, uint32_t del, const uint64_t *data, size_t n) {
    rx_buf_fmt(out, "%u %u %zu\n", (unsigned)start, (unsigned)del, n);
    for (size_t i = 0; i < n; i++) rx_buf_fmt(out, i ? " %llu" : "%llu", (unsigned long long)data[i]);
    if (n) rx_buf_add(out, "\n", 1);
}

static int rx_stream_records(rx_stream *s, rx_buf *recs, int *n, int *full) {
    rx_buf b = {NULL, 0, 0};
    rx_records_finish(recs, n, full, &b);
    int r = rx_stream_part(s, b.data, b.len);
    rx_buf_free(&b);
    return r;
}
int rx_stream_hierarchy(rx_stream *s, rx_hierarchy *l) {
    int roots = l->roots, r = rx_stream_records(s, &l->recs, &l->n, &l->full);
    l->roots = roots;
    return r;
}
int rx_stream_calls(rx_stream *s, rx_calls *l) { return rx_stream_records(s, &l->recs, &l->n, &l->full); }
int rx_stream_workspace_symbols(rx_stream *s, rx_workspace_symbols *l) {
    return rx_stream_records(s, &l->recs, &l->n, &l->full);
}

/* a diagnostic id: 1 to 64 of [A-Za-z0-9_.:-] */
static int rx_diagnostic_id(const char *s) {
    size_t n = s ? strlen(s) : 0;
    if (n < 1 || n > 64) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
              c == ':' || c == '-'))
            return 0;
    }
    return 1;
}

/* a diagnostic's range as rhun takes it: positions of at least 1 1, the end not before the start */
static rx_range rx_mark_range(rx_range r) {
    if (r.start.line < 1) r.start.line = 1;
    if (r.start.col < 1) r.start.col = 1;
    if (r.end.line < 1) r.end.line = 1;
    if (r.end.col < 1) r.end.col = 1;
    if (rx_before(r.end, r.start)) r.end = r.start;
    return r;
}

/* a record "NAME LEN" and a one-line text cut at max bytes, unless it is empty */
static void rx_text_record(rx_buf *b, const char *name, const char *s, size_t max) {
    size_t n = s ? rx_cut(s, strlen(s), max) : 0;
    if (!n) return;
    rx_buf_fmt(b, "%s %zu\n", name, n);
    rx_add_line(b, s, n);
}

void rx_file_diagnostics_records(const rx_diagnostic *diags, int n, rx_buf *out, int *count) {
    static const char *const sevs[] = {"error", "warning", "info", "hint"};
    const char **ids = (const char **)calloc((size_t)(n > 0 ? n : 1), sizeof *ids);
    if (!ids) abort();
    int kept = 0, nids = 0;
    size_t start = out->len;
    for (int i = 0; i < n && kept < RX_DIAGNOSTICS; i++) {
        const rx_diagnostic *d = &diags[i];
        const char *sev = d->severity ? d->severity : "error";
        int known = 0;
        for (int k = 0; k < 4; k++) known |= strcmp(sev, sevs[k]) == 0;
        size_t ml = d->message ? rx_cut(d->message, strlen(d->message), RX_DIAGNOSTIC_MESSAGE) : 0;
        if (!known || !ml) continue;
        rx_buf x = {NULL, 0, 0};
        int fresh = rx_diagnostic_id(d->id);
        for (int k = 0; fresh && k < nids; k++) fresh = strcmp(ids[k], d->id) != 0;
        if (fresh) rx_buf_fmt(&x, "id %zu\n%s", strlen(d->id), d->id);
        rx_text_record(&x, "code", d->code, 100);
        rx_text_record(&x, "source", d->source, 100);
        if (d->href && rx_url(d->href, 1000)) rx_buf_fmt(&x, "href %zu\n%s", strlen(d->href), d->href);
        if (d->unnecessary) rx_buf_str(&x, "tag unnecessary 0\n");
        if (d->deprecated) rx_buf_str(&x, "tag deprecated 0\n");
        for (int k = 0, related = 0; d->related && k < d->nrelated && related < RX_RELATED; k++) {
            const rx_related *r = &d->related[k];
            if (!rx_valid_target(r->path, r->doc)) continue;
            char tmp[32];
            const char *t = rx_target(r->path, r->doc, tmp, sizeof tmp), *m = r->message ? r->message : "";
            size_t tl = strlen(t), mml = rx_cut(m, strlen(m), RX_DIAGNOSTIC_MESSAGE);
            rx_buf_str(&x, "related ");
            rx_add_range(&x, rx_mark_range(r->range));
            rx_buf_fmt(&x, " %zu %zu\n", tl, mml);
            rx_buf_add(&x, t, tl);
            rx_add_line(&x, m, mml);
            related++;
        }
        rx_buf h = {NULL, 0, 0};
        rx_add_range(&h, rx_mark_range(d->range));
        rx_buf_fmt(&h, " %s %zu %zu\n", sev, ml, x.len);
        int fits = out->len - start + h.len + ml + x.len + RX_PATH_MAX <= RX_DIAGNOSTICS_BODY;
        if (fits) {
            rx_buf_add(out, h.data, h.len);
            rx_buf_add(out, d->message, ml);
            rx_buf_add(out, x.data, x.len);
            if (fresh) ids[nids++] = d->id;
            kept++;
        }
        rx_buf_free(&h);
        rx_buf_free(&x);
        if (!fits) break;
    }
    free(ids);
    *count = kept;
}

int rx_file_diagnostics(rx_ext *ext, const char *path, rx_doc doc, int64_t version, const rx_diagnostic *diags,
                        int n) {
    if (!rx_has(ext, "file-diagnostics")) {
        snprintf(ext->error, sizeof ext->error, "unknown file-diagnostics was not agreed");
        return -1;
    }
    int is_path = path && *path;
    if (!rx_valid_target(path, doc)) {
        snprintf(ext->error, sizeof ext->error, "syntax a target is an absolute path or an open document");
        return -1;
    }
    if (!is_path && version < 0) {
        snprintf(ext->error, sizeof ext->error, "syntax an open document's diagnostics need its version");
        return -1;
    }
    char tmp[32], v[32] = "-";
    const char *t = rx_target(path, doc, tmp, sizeof tmp);
    if (!is_path) snprintf(v, sizeof v, "%lld", (long long)version);
    rx_buf body = {NULL, 0, 0}, line = {NULL, 0, 0};
    int count = 0;
    rx_buf_str(&body, t);
    rx_file_diagnostics_records(diags, n, &body, &count);
    rx_buf_fmt(&line, "file-diagnostics %s %d %zu %zu", v, count, strlen(t), body.len);
    int r = rx_request(ext, line.data, body.data, body.len, NULL, NULL);
    rx_buf_free(&line);
    rx_buf_free(&body);
    return r;
}

int rx_reset_diagnostics(rx_ext *ext) {
    if (!rx_has(ext, "file-diagnostics")) {
        snprintf(ext->error, sizeof ext->error, "unknown file-diagnostics was not agreed");
        return -1;
    }
    return rx_request(ext, "file-diagnostics-reset", NULL, 0, NULL, NULL);
}

static void rx_answer_ai(rx_ext *ext, const rx_handlers *h, void *user, char **f, int n,
                         const unsigned char *in, size_t in_len, int *ok, rx_buf *body);

static void rx_answer(rx_ext *ext, const rx_handlers *h, void *user, char **f, int n,
                      const unsigned char *data, size_t data_len) {
    /* ID VERB DOC VERSION, then LINE COL KIND CHAR (12b to 12e), L1 C1 L2 C2 (actions), LINE COL
     * invoked NAME (rename), TAB_WIDTH SPACES (format) or LEN and a body (complete-resolve) */
    const char *verb = f[1];
    int ok = 0, refused = 0, opt[4];
    rx_buf body = {NULL, 0, 0}, line = {NULL, 0, 0};
    rx_wedit e;
    memset(&e, 0, sizeof e);
    int caps = rx_edit_caps(ext);
    /* the stream of this request: cancelled by a cancel event that came already, or comes while
     * a request of the handler's waits */
    rx_stream stream = {ext, atoll(f[0])};
    ext->query_id = stream.id;
    ext->query_cancelled = 0;
    for (rx_msg *q = ext->queue; q; q = q->next)
        if (rx_cancels(q->line, f[0])) ext->query_cancelled = 1;
    int is_pos = strcmp(verb, "definition") == 0 || strcmp(verb, "references") == 0 || strcmp(verb, "symbols") == 0;
    if ((strcmp(verb, "complete") == 0 || strcmp(verb, "signature") == 0 || strcmp(verb, "signatures") == 0) && n >= 8) {
        rx_ask req;
        req.id = atoll(f[0]);
        req.doc = atoll(f[2]);
        req.version = atoll(f[3]);
        req.at.line = atoi(f[4]);
        req.at.col = atoi(f[5]);
        req.kind = f[6];
        req.ch = strcmp(f[6], "trigger") == 0 ? f[7] : "";
        if (strcmp(verb, "complete") == 0 && h->complete_stream) ok = h->complete_stream(ext, &req, &stream, &body, user);
        else if (strcmp(verb, "complete") == 0 && h->complete) ok = h->complete(ext, &req, &body, user);
        else if (strcmp(verb, "signature") == 0 && h->signature) ok = h->signature(ext, &req, &body, user);
        else if (strcmp(verb, "signatures") == 0 && h->signatures && rx_has(ext, "signature-list"))
            ok = h->signatures(ext, &req, &body, user);
    } else if (is_pos && n >= 6 && rx_nums(f, 2, 6)) {
        rx_ask req;
        req.id = atoll(f[0]);
        req.doc = atoll(f[2]);
        req.version = atoll(f[3]);
        req.at.line = atoi(f[4]);
        req.at.col = atoi(f[5]);
        req.kind = n > 6 ? f[6] : "invoked";
        req.ch = "";
        if (verb[0] == 's') {
            rx_symbols l;
            memset(&l, 0, sizeof l);
            if (h->symbols && (ok = h->symbols(ext, &req, &l, user)) != 0) rx_symbols_finish(&l, &body);
            rx_buf_free(&l.recs);
        } else {
            rx_locations l;
            memset(&l, 0, sizeof l);
            int (*fn)(rx_ext *, const rx_ask *, rx_locations *, void *) = verb[0] == 'd' ? h->definition : h->references;
            int (*sfn)(rx_ext *, const rx_ask *, rx_stream *, rx_locations *, void *) =
                verb[0] == 'd' ? h->definition_stream : h->references_stream;
            if (sfn) ok = sfn(ext, &req, &stream, &l, user);
            else if (fn) ok = fn(ext, &req, &l, user);
            if (ok) rx_locations_finish(&l, &body);
            rx_buf_free(&l.recs);
        }
    } else if (strcmp(verb, "actions") == 0 && n >= 8 && rx_nums(f, 2, 8) && h->actions) {
        rx_actions_ask req;
        req.id = atoll(f[0]);
        req.doc = atoll(f[2]);
        req.version = atoll(f[3]);
        req.range.start.line = atoi(f[4]);
        req.range.start.col = atoi(f[5]);
        req.range.end.line = atoi(f[6]);
        req.range.end.col = atoi(f[7]);
        rx_actions l;
        memset(&l, 0, sizeof l);
        l.caps = rx_builder_caps(caps);
        if ((ok = h->actions(ext, &req, &l, user)) != 0) rx_actions_finish(&l, &body);
        rx_buf_free(&l.recs);
    } else if (strcmp(verb, "rename") == 0 && n >= 8 && rx_nums(f, 2, 6) && h->rename) {
        rx_rename_ask req;
        req.id = atoll(f[0]);
        req.doc = atoll(f[2]);
        req.version = atoll(f[3]);
        req.at.line = atoi(f[4]);
        req.at.col = atoi(f[5]);
        req.name = f[7];
        if ((ok = h->rename(ext, &req, &e, user)) != 0) rx_wedit_finish_caps(&e, &body, caps & 1, (caps & 2) != 0, NULL);
    } else if (strcmp(verb, "format") == 0 && n >= 6 && rx_nums(f, 2, 5) && h->format &&
               (strcmp(f[5], "yes") == 0 || strcmp(f[5], "no") == 0)) {
        rx_format_ask req;
        req.id = atoll(f[0]);
        req.doc = atoll(f[2]);
        req.version = atoll(f[3]);
        req.tab_width = atoi(f[4]);
        req.spaces = strcmp(f[5], "yes") == 0;     /* format keeps 12g's grammar */
        if ((ok = h->format(ext, &req, &e, user)) != 0) rx_wedit_finish_caps(&e, &body, 0, 0, "a format answer");
    } else if (strcmp(verb, "content") == 0 && n >= 3 && h->content && rx_has(ext, "uris")) {
        rx_buf uri = {NULL, 0, 0};
        for (int i = 2; i < n; i++) {
            if (i > 2) rx_buf_add(&uri, " ", 1);
            rx_buf_str(&uri, f[i]);
        }
        rx_content c;
        memset(&c, 0, sizeof c);
        if (h->content(ext, uri.data, &c, user)) {
            const char *lang = c.lang ? c.lang : "", *title = c.title ? c.title : "";
            if (strlen(lang) > 255 || strlen(title) > 255 || c.text_len > (4u << 20)) {
                e.fail = "toobig";
                snprintf(e.why, sizeof e.why, "a language or title over 255 bytes or a text over 4 MiB");
            } else {
                rx_buf_fmt(&body, "%zu %zu %zu\n", strlen(lang), strlen(title), c.text_len);
                for (const char *p = lang; *p; p++) rx_buf_add(&body, *p == ' ' ? "_" : p, 1);
                rx_buf_str(&body, title);
                rx_buf_add(&body, c.text ? c.text : "", c.text_len);
            }
            ok = 1;
        } else {
            e.fail = "nofile";
            snprintf(e.why, sizeof e.why, "no such document");
            ok = 1;
        }
        rx_buf_free(&uri);
    } else if (strcmp(verb, "save-actions") == 0 && n == 5 && h->save_actions && rx_has(ext, "save-actions")) {
        rx_save_actions_ask req;
        if (rx_positive_num(f[0], 2147483647, &req.id) && req.id > 0 &&
            rx_positive_num(f[2], INT64_MAX, &req.doc) && req.doc > 0 &&
            rx_positive_num(f[3], INT64_MAX, &req.version) && rx_source_kind(f[4])) {
            req.kind = f[4];
            if ((ok = h->save_actions(ext, &req, &e, user)) != 0) rx_wedit_finish_caps(&e, &body, 0, 0, "a save-actions answer");
        }
    } else if (strcmp(verb, "vcopy") == 0 && n >= 4 && h->view_copy && rx_has(ext, "view-keys")) {
        ok = h->view_copy(ext, atoll(f[2]), strcmp(f[3], "cut") == 0, &body, user) ? 1 : 0;
        if (!ok) ok = 1;            /* nothing: ok ID 0 leaves the clipboard */
    } else if (strcmp(verb, "quickdiff") == 0 && n == 4 && h->quickdiff && rx_has(ext, "quickdiff")) {
        rx_quickdiff_ask req;
        if (rx_positive_num(f[0], 2147483647, &req.id) && req.id > 0 &&
            rx_positive_num(f[2], INT64_MAX, &req.doc) && req.doc > 0 &&
            rx_positive_num(f[3], INT64_MAX, &req.version))
            ok = h->quickdiff(ext, &req, &body, user);
    } else if (strcmp(verb, "call") == 0 && n == 6 && h->call && rx_has(ext, "command-args") &&
               strchr(f[2], '.') && f[4][0]) {
        rx_call_ask req;
        if (rx_positive_num(f[0], 2147483647, &req.id) && req.id > 0 && rx_positive_num(f[3], INT64_MAX, &req.doc)) {
            req.command = f[2];
            req.name = strchr(f[2], '.') + 1;
            req.caller = f[4];
            req.args = data ? data : (const unsigned char *)"";
            req.args_len = data_len;
            rx_buf out = {NULL, 0, 0};
            const char *code = "refused";
            int r = h->call(ext, &req, &out, &code, user);
            if (r > 0 && out.len > (1u << 20)) {
                e.fail = "toobig";
                snprintf(e.why, sizeof e.why, "a call's result is at most 1048576 bytes");
                ok = 1;
            } else if (r > 0) {
                rx_buf_add(&body, out.data ? out.data : "", out.len);
                ok = 1;
            } else if (r < 0) {
                size_t cl = code ? strlen(code) : 0;
                e.fail = cl && cl <= 40 && strspn(code, "abcdefghijklmnopqrstuvwxyz") == cl ? code : "refused";
                size_t w = 0;
                for (size_t i = 0; i < out.len && w + 1 < sizeof e.why; i++)
                    e.why[w++] = out.data[i] == '\n' || out.data[i] == '\r' ? ' ' : out.data[i];
                e.why[w] = 0;
                ok = 1;
            }
            rx_buf_free(&out);
        }
    } else if (strcmp(verb, "debug-resolve") == 0 && n == 6 && h->debug_resolve && rx_has(ext, "debug")) {
        rx_debug_resolve_ask req;
        memset(&req, 0, sizeof req);
        req.type = f[2];
        req.request = f[3];
        req.no_debug = strcmp(f[4], "1") == 0;
        if (rx_positive_num(f[0], 2147483647, &req.id) && req.id > 0 &&
            (strcmp(f[3], "launch") == 0 || strcmp(f[3], "attach") == 0) &&
            rx_debug_config_parse(data, data_len, &req.config, &req.ctx) == 0) {
            rx_debug_config out;
            memset(&out, 0, sizeof out);
            rx_buf why = {NULL, 0, 0};
            int r = h->debug_resolve(ext, &req, &out, &why, user);
            ok = 1;
            if (r == 1 && rx_debug_config_encode(&out, &body) < 0) {
                e.fail = "toobig";
                snprintf(e.why, sizeof e.why, "the configuration is over 1 MB");
            } else if (r == -2) {
                e.fail = "cancelled";
                snprintf(e.why, sizeof e.why, "the launch was cancelled");
            } else if (r < 0) {
                e.fail = "refused";
                size_t w = 0;
                for (size_t i = 0; i < why.len && w + 1 < sizeof e.why; i++)
                    e.why[w++] = why.data[i] == '\n' || why.data[i] == '\r' ? ' ' : why.data[i];
                e.why[w] = 0;
            }
            rx_debug_config_free(&out);
            rx_debug_config_free(&req.config);
            rx_buf_free(&req.ctx.path);
            rx_buf_free(&why);
        }
    } else if (strcmp(verb, "commit-message") == 0 && n == 3 && h->commit_message && rx_has(ext, "commit-message")) {
        rx_commit_message_ask req;
        if (rx_positive_num(f[0], 2147483647, &req.id) && req.id > 0 &&
            rx_parse_commit_message(req.id, data, data_len, &req) == 0) {
            rx_buf msg = {NULL, 0, 0};
            int r = h->commit_message(ext, &req, &msg, user);
            size_t start = 0, len = 0;
            if (r > 0 && rx_check_commit_message(msg.data, msg.len, &start, &len) == 0) {
                rx_buf_add(&body, msg.data + start, len);
                ok = 1;
            } else if (r != 0) {
                /* refused: the handler's reason, or rhun's check, on one line */
                e.fail = "refused";
                size_t w = 0;
                const char *why = r < 0 && msg.len ? msg.data : "an empty, oversized or invalid commit message";
                size_t wl = r < 0 && msg.len ? msg.len : strlen(why);
                for (size_t i = 0; i < wl && w + 1 < sizeof e.why; i++) e.why[w++] = why[i] == '\n' || why[i] == '\r' ? ' ' : why[i];
                e.why[w] = 0;
                ok = 1;
            }
            rx_buf_free(&msg);
        }
    } else if (strcmp(verb, "linked") == 0 && n == 6 && rx_nums(f, 2, 6) && h->linked && rx_has(ext, "linked-editing")) {
        rx_linked_ask req;
        req.id = atoll(f[0]);
        req.doc = atoll(f[2]);
        req.version = atoll(f[3]);
        req.at.line = atoi(f[4]);
        req.at.col = atoi(f[5]);
        rx_linked l;
        memset(&l, 0, sizeof l);
        if (req.at.line >= 1 && req.at.col >= 1 && (ok = h->linked(ext, &req, &l, user)) != 0) rx_linked_finish(&l, &body);
        rx_buf_free(&l.ranges);
    } else if (strcmp(verb, "complete-resolve") == 0 && n == 5 && rx_nums(f, 2, 5) && h->complete_resolve &&
               rx_has(ext, "complete-items") && (size_t)atoll(f[4]) == data_len) {
        rx_resolve_ask req;
        req.id = stream.id;
        req.doc = atoll(f[2]);
        req.version = atoll(f[3]);
        req.data = data ? data : (const unsigned char *)"";
        req.len = data_len;
        ok = h->complete_resolve(ext, &req, &body, user);
    } else if ((strcmp(verb, "declaration") == 0 || strcmp(verb, "type-definition") == 0 ||
                strcmp(verb, "implementation") == 0) && n >= 6 && rx_nums(f, 2, 6) && rx_has(ext, "navigation")) {
        int (*fn)(rx_ext *, const rx_ask *, rx_stream *, rx_locations *, void *) =
            verb[0] == 'd' ? h->declaration : verb[0] == 't' ? h->type_definition : h->implementation;
        rx_ask req;
        req.id = stream.id;
        req.doc = atoll(f[2]);
        req.version = atoll(f[3]);
        req.at.line = atoi(f[4]);
        req.at.col = atoi(f[5]);
        req.kind = n > 6 ? f[6] : "invoked";
        req.ch = "";
        rx_locations l;
        memset(&l, 0, sizeof l);
        if (fn && (ok = fn(ext, &req, &stream, &l, user)) != 0) rx_locations_finish(&l, &body);
        rx_buf_free(&l.recs);
    } else if (strcmp(verb, "prepare-rename") == 0 && n >= 6 && rx_nums(f, 2, 6) && h->prepare_rename &&
               rx_has(ext, "prepare-rename")) {
        rx_ask req;
        req.id = stream.id;
        req.doc = atoll(f[2]);
        req.version = atoll(f[3]);
        req.at.line = atoi(f[4]);
        req.at.col = atoi(f[5]);
        req.kind = n > 6 ? f[6] : "invoked";
        req.ch = "";
        ok = h->prepare_rename(ext, &req, &body, user);
        refused = ok < 0;
    } else if (strcmp(verb, "range-format") == 0 && n == 12 && rx_nums(f, 2, 8) && rx_format_options(f, 8, opt) &&
               h->range_format && rx_has(ext, "partial-format")) {
        rx_range_format_ask req;
        req.id = stream.id;
        req.doc = atoll(f[2]);
        req.version = atoll(f[3]);
        req.range.start.line = atoi(f[4]);
        req.range.start.col = atoi(f[5]);
        req.range.end.line = atoi(f[6]);
        req.range.end.col = atoi(f[7]);
        req.tab_width = opt[0], req.spaces = opt[1], req.trim = opt[2], req.final_newline = opt[3];
        if ((ok = h->range_format(ext, &req, &e, user)) != 0) rx_wedit_finish_caps(&e, &body, 0, 0, "a range-format answer");
    } else if (strcmp(verb, "type-format") == 0 && n == 11 && rx_nums(f, 2, 6) && rx_format_options(f, 7, opt) &&
               h->type_format && rx_has(ext, "partial-format")) {
        rx_type_format_ask req;
        req.id = stream.id;
        req.doc = atoll(f[2]);
        req.version = atoll(f[3]);
        req.at.line = atoi(f[4]);
        req.at.col = atoi(f[5]);
        req.ch = f[6];
        req.tab_width = opt[0], req.spaces = opt[1], req.trim = opt[2], req.final_newline = opt[3];
        if ((ok = h->type_format(ext, &req, &e, user)) != 0) rx_wedit_finish_caps(&e, &body, 0, 0, "a type-format answer");
    } else if (strcmp(verb, "code-actions") == 0 && n == 11 && rx_nums(f, 2, 8) &&
               (strcmp(f[8], "invoked") == 0 || strcmp(f[8], "auto") == 0) && h->code_actions &&
               rx_has(ext, "code-actions")) {
        rx_code_actions_ask req;
        req.id = stream.id;
        req.doc = atoll(f[2]);
        req.version = atoll(f[3]);
        req.range.start.line = atoi(f[4]);
        req.range.start.col = atoi(f[5]);
        req.range.end.line = atoi(f[6]);
        req.range.end.col = atoi(f[7]);
        req.trigger = f[8];
        req.kinds = strcmp(f[9], "-") == 0 ? "" : f[9];
        req.diagnostics = strcmp(f[10], "-") == 0 ? "" : f[10];
        rx_code_actions l;
        memset(&l, 0, sizeof l);
        l.kinds = req.kinds;
        l.caps = rx_builder_caps(caps);
        if ((ok = h->code_actions(ext, &req, &l, user)) != 0) rx_code_actions_finish(&l, &body);
        rx_buf_free(&l.recs);
    } else if (strcmp(verb, "code-action-resolve") == 0 && n == 5 && rx_nums(f, 2, 5) && h->code_action_resolve &&
               rx_has(ext, "code-actions") && (size_t)atoll(f[4]) == data_len) {
        rx_resolve_ask req;
        req.id = stream.id;
        req.doc = atoll(f[2]);
        req.version = atoll(f[3]);
        req.data = data ? data : (const unsigned char *)"";
        req.len = data_len;
        rx_resolved r;
        memset(&r, 0, sizeof r);
        r.caps = rx_builder_caps(caps);
        if ((ok = h->code_action_resolve(ext, &req, &r, user)) != 0 && rx_resolved_finish(&r, &body) < 0) {
            e.fail = r.edit.fail;               /* refused, or past a workspace edit's limits */
            memcpy(e.why, r.edit.why, sizeof e.why);
        }
        rx_wedit_free(&r.edit);
        rx_buf_free(&r.command);
    } else if (strcmp(verb, "hover-query") == 0 && n >= 7 && rx_nums(f, 2, 6) && h->hover && rx_has(ext, "hover-query")) {
        rx_ask req;
        req.id = stream.id;
        req.doc = atoll(f[2]);
        req.version = atoll(f[3]);
        req.at.line = atoi(f[4]);
        req.at.col = atoi(f[5]);
        req.kind = f[6];
        req.ch = "";
        ok = h->hover(ext, &req, &body, user);
    } else if (strcmp(verb, "selection-ranges") == 0 && n == 6 && rx_nums(f, 2, 6) && h->selection_ranges &&
               rx_has(ext, "selection-ranges") && (size_t)atoll(f[5]) == data_len && atoi(f[4]) >= 1 &&
               atoi(f[4]) <= RX_SELECTION_MAX) {
        rx_selections *sel = (rx_selections *)calloc(1, sizeof *sel);
        if (!sel) abort();
        const char *p = data ? (const char *)data : "";
        int want = atoi(f[4]), got = 0, good = 1;
        while (*p && good) {
            char *end;
            long l = strtol(p, &end, 10);
            good = end != p && *end == ' ' && l >= 0;
            p = good ? end + 1 : p;
            long c = good ? strtol(p, &end, 10) : 0;
            good = good && end != p && (*end == '\n' || *end == 0) && c >= 0 && got < want;
            if (good) {
                sel->at[got].line = (int)l, sel->at[got].col = (int)c, got++;
                p = *end ? end + 1 : end;
            }
        }
        if (good && got == want) {
            rx_selection_ask req;
            req.id = stream.id;
            req.doc = atoll(f[2]);
            req.version = atoll(f[3]);
            req.n = got;
            req.positions = sel->at;
            sel->npos = got;
            if ((ok = h->selection_ranges(ext, &req, sel, user)) != 0) rx_selections_finish(sel, &body);
        }
        rx_buf_free(&sel->ranges);
        free(sel);
    } else if ((strcmp(verb, "call-hierarchy") == 0 || strcmp(verb, "type-hierarchy") == 0) && n >= 6 &&
               rx_nums(f, 2, 6) && rx_has(ext, "hierarchy")) {
        int (*fn)(rx_ext *, const rx_ask *, rx_hierarchy *, void *) = verb[0] == 'c' ? h->call_hierarchy : h->type_hierarchy;
        rx_ask req;
        req.id = stream.id;
        req.doc = atoll(f[2]);
        req.version = atoll(f[3]);
        req.at.line = atoi(f[4]);
        req.at.col = atoi(f[5]);
        req.kind = n > 6 ? f[6] : "invoked";
        req.ch = "";
        rx_hierarchy l;
        memset(&l, 0, sizeof l);
        l.roots = 1;
        if (fn && (ok = fn(ext, &req, &l, user)) != 0) rx_hierarchy_finish(&l, &body);
        rx_buf_free(&l.recs);
    } else if ((strcmp(verb, "calls-in") == 0 || strcmp(verb, "calls-out") == 0 || strcmp(verb, "supertypes") == 0 ||
                strcmp(verb, "subtypes") == 0) && n == 3 && rx_nums(f, 2, 3) && rx_has(ext, "hierarchy") &&
               (size_t)atoll(f[2]) == data_len) {
        rx_data_ask req;
        req.id = stream.id;
        req.data = data ? data : (const unsigned char *)"";
        req.len = data_len;
        if (strncmp(verb, "calls-", 6) == 0) {
            int (*fn)(rx_ext *, const rx_data_ask *, rx_stream *, rx_calls *, void *) =
                verb[6] == 'i' ? h->calls_in : h->calls_out;
            rx_calls l;
            memset(&l, 0, sizeof l);
            if (fn && (ok = fn(ext, &req, &stream, &l, user)) != 0) rx_calls_finish(&l, &body);
            rx_buf_free(&l.recs);
        } else {
            int (*fn)(rx_ext *, const rx_data_ask *, rx_stream *, rx_hierarchy *, void *) =
                verb[1] == 'u' && verb[2] == 'p' ? h->supertypes : h->subtypes;
            rx_hierarchy l;
            memset(&l, 0, sizeof l);
            if (fn && (ok = fn(ext, &req, &stream, &l, user)) != 0) rx_hierarchy_finish(&l, &body);
            rx_buf_free(&l.recs);
        }
    } else if (strcmp(verb, "workspace-symbols") == 0 && n == 4 && rx_nums(f, 2, 4) && h->workspace_symbols &&
               rx_has(ext, "workspace-symbols") && (size_t)atoll(f[3]) == data_len) {
        rx_workspace_symbols_ask req;
        req.id = stream.id;
        req.query = data ? (const char *)data : "";
        req.len = data_len;
        req.max = atoi(f[2]);
        rx_workspace_symbols l;
        memset(&l, 0, sizeof l);
        if ((ok = h->workspace_symbols(ext, &req, &stream, &l, user)) != 0) rx_workspace_symbols_finish(&l, &body);
        rx_buf_free(&l.recs);
    } else if (strcmp(verb, "document-links") == 0 && n == 4 && rx_nums(f, 2, 4) && h->document_links &&
               rx_has(ext, "document-links")) {
        rx_doc_ask req;
        req.id = stream.id;
        req.doc = atoll(f[2]);
        req.version = atoll(f[3]);
        rx_links l;
        memset(&l, 0, sizeof l);
        if ((ok = h->document_links(ext, &req, &l, user)) != 0) rx_links_finish(&l, &body);
        rx_buf_free(&l.recs);
    } else if ((strcmp(verb, "folding-ranges") == 0 && n == 4 && rx_nums(f, 2, 4) && h->folding_ranges &&
                rx_has(ext, "folding-ranges")) ||
               ((strcmp(verb, "inlay-hints") == 0 || strcmp(verb, "code-lens") == 0 || strcmp(verb, "colors") == 0) &&
                n == 6 && rx_nums(f, 2, 6))) {
        rx_pres l;
        memset(&l, 0, sizeof l);
        if (verb[0] == 'f') {
            rx_doc_ask req;
            req.id = stream.id;
            req.doc = atoll(f[2]);
            req.version = atoll(f[3]);
            ok = h->folding_ranges(ext, &req, &l, user);
        } else {
            rx_lines_ask req;
            req.id = stream.id;
            req.doc = atoll(f[2]);
            req.version = atoll(f[3]);
            req.first = atoi(f[4]);
            req.last = atoi(f[5]);
            if (verb[0] == 'i' && h->inlay_hints && rx_has(ext, "inlay-hints")) ok = h->inlay_hints(ext, &req, &l, user);
            else if (verb[1] == 'o' && verb[2] == 'd' && h->code_lenses && rx_has(ext, "code-lens"))
                ok = h->code_lenses(ext, &req, &l, user);
            else if (verb[1] == 'o' && verb[2] == 'l' && h->colors && rx_has(ext, "document-colors"))
                ok = h->colors(ext, &req, &l, user);
        }
        if (ok) rx_pres_finish(&l, &body);
        rx_buf_free(&l.recs);
    } else if ((strcmp(verb, "semantic-tokens") == 0 && n == 5 && rx_nums(f, 2, 4)) ||
               (strcmp(verb, "semantic-tokens-range") == 0 && n == 6 && rx_nums(f, 2, 6))) {
        rx_tokens_ask req;
        req.id = stream.id;
        req.doc = atoll(f[2]);
        req.version = atoll(f[3]);
        req.range = verb[15] == '-';
        req.prev = req.range || strcmp(f[4], "-") == 0 ? "" : f[4];
        req.first = req.range ? atoi(f[4]) : 0;
        req.last = req.range ? atoi(f[5]) : 0;
        if (h->semantic_tokens && rx_has(ext, "semantic-tokens")) ok = h->semantic_tokens(ext, &req, &body, user);
    } else if ((strcmp(verb, "highlights") == 0 && n == 6 && rx_nums(f, 2, 6) && h->highlights &&
                rx_has(ext, "document-highlights")) ||
               (strcmp(verb, "inline-complete") == 0 && n == 7 && rx_nums(f, 2, 6) && h->inline_complete &&
                rx_has(ext, "inline-complete"))) {
        rx_ask req;
        req.id = stream.id;
        req.doc = atoll(f[2]);
        req.version = atoll(f[3]);
        req.at.line = atoi(f[4]);
        req.at.col = atoi(f[5]);
        req.kind = n > 6 ? f[6] : "invoked";
        req.ch = "";
        rx_pres l;
        memset(&l, 0, sizeof l);
        ok = verb[0] == 'h' ? h->highlights(ext, &req, &l, user) : h->inline_complete(ext, &req, &l, user);
        if (ok) rx_pres_finish(&l, &body);
        rx_buf_free(&l.recs);
    } else if (strcmp(verb, "color-edit") == 0 && n == 9 && rx_nums(f, 2, 8) && h->color_edit &&
               rx_has(ext, "document-colors") && strlen(f[8]) == 8) {
        rx_color_ask req;
        req.id = stream.id;
        req.doc = atoll(f[2]);
        req.version = atoll(f[3]);
        req.r.start.line = atoi(f[4]);
        req.r.start.col = atoi(f[5]);
        req.r.end.line = atoi(f[6]);
        req.r.end.col = atoi(f[7]);
        req.rgba = (uint32_t)strtoul(f[8], NULL, 16);
        if ((ok = h->color_edit(ext, &req, &e, user)) != 0) rx_wedit_finish_caps(&e, &body, caps & 1, (caps & 2) != 0, NULL);
    } else {
        rx_answer_ai(ext, h, user, f, n, data, data_len, &ok, &body);
    }
    ext->query_id = 0;
    ext->query_cancelled = 0;
    if (refused) {
        rx_buf_fmt(&line, "error %s refused%s", f[0], body.len ? " " : "");
        rx_buf_add(&line, body.data, body.len);     /* one line (rx_refuse_answer) */
        rx_write(line.data, NULL, 0);
    } else if (ok < 0 && !e.fail) {        /* an AI handler's "CODE TEXT" */
        rx_buf_add(&body, "", 1);
        for (char *c = body.data; *c; c++)
            if (*c == '\n' || *c == '\r') *c = ' ';
        rx_buf_fmt(&line, "error %s %s", f[0], body.data[0] ? body.data : "refused");
        rx_write(line.data, NULL, 0);
    } else if (ok && e.fail) {
        if (e.why[0]) rx_buf_fmt(&line, "error %s %s %s", f[0], e.fail, e.why);     /* why is one line */
        else rx_buf_fmt(&line, "error %s %s", f[0], e.fail);
        rx_write(line.data, NULL, 0);
    } else if (ok) {
        rx_buf_fmt(&line, "ok %s %zu", f[0], body.len);
        rx_write(line.data, body.data, body.len);
    } else {
        rx_buf_fmt(&line, "error %s unknown no such request", f[0]);
        rx_write(line.data, NULL, 0);
    }
    rx_wedit_free(&e);
    rx_buf_free(&line);
    rx_buf_free(&body);
}

/* ---- retained views ---- */

void rx_rec(rx_buf *b, const char *head, const void *data, size_t len) {
    rx_buf_fmt(b, "%s %zu\n", head, len);
    if (len) rx_buf_add(b, data, len);
}

static size_t rx_line_len(const char *s, size_t max) {
    size_t n = 0;
    while (s[n] && s[n] != '\n' && s[n] != '\r' && n < max) n++;
    while (n < strlen(s) && n > 0 && n == max && ((unsigned char)s[n] & 0xc0) == 0x80) n--;
    return n;
}

void rx_x_desc(rx_buf *x, const char *text) { rx_rec(x, "desc", text, rx_line_len(text, 1000)); }
void rx_x_tip(rx_buf *x, const char *text) { size_t n = strlen(text); rx_rec(x, "tip", text, n > 4000 ? 4000 : n); }
void rx_x_icon(rx_buf *x, const char *spec, const char *color) {
    rx_buf h = {NULL, 0, 0};
    rx_buf_fmt(&h, "icon %s %s", spec, color && *color ? color : "-");
    rx_rec(x, h.data, NULL, 0);
    rx_buf_free(&h);
}
void rx_x_color(rx_buf *x, const char *color) { rx_buf_fmt(x, "color %s 0\n", color); }
void rx_x_hl(rx_buf *x, int from, int to) { rx_buf_fmt(x, "hl %d %d 0\n", from, to); }
void rx_x_badge(rx_buf *x, int n) { rx_buf_fmt(x, "badge %d 0\n", n); }
void rx_x_check(rx_buf *x, int on) { rx_buf_fmt(x, "check %d 0\n", on ? 1 : 0); }
void rx_x_ctx(rx_buf *x, const char *value) { size_t n = strlen(value); rx_rec(x, "ctx", value, n > 100 ? 100 : n); }
void rx_x_data(rx_buf *x, const void *data, size_t len) { rx_rec(x, "data", data, len > 4096 ? 4096 : len); }
void rx_x_open(rx_buf *x, const char *path, int line, int col) {
    rx_buf h = {NULL, 0, 0};
    rx_buf_fmt(&h, "open %d %d", line, col);
    rx_rec(x, h.data, path, strlen(path));
    rx_buf_free(&h);
}
void rx_x_command(rx_buf *x, const char *name, const void *args, size_t len) {
    rx_buf h = {NULL, 0, 0};
    rx_buf_fmt(&h, "command %s", name);
    rx_rec(x, h.data, args, len);
    rx_buf_free(&h);
}

int rx_node_rec(rx_buf *out, const char *id, size_t idlen, const char *label, const char *kids, const rx_buf *extra) {
    if (idlen == 0 || idlen > 1000 || memchr(id, '\n', idlen) || memchr(id, 0, idlen)) return -1;
    size_t ln = rx_line_len(label, 1000);
    size_t xn = extra ? extra->len : 0;
    rx_buf_fmt(out, "%zu %zu %zu %s\n", idlen, ln, xn, kids);
    rx_buf_add(out, id, idlen);
    rx_buf_add(out, label, ln);
    if (xn) rx_buf_add(out, extra->data, xn);
    return 0;
}

static void rx_patch_op(rx_patch *p, const char *head, const void *data, size_t len) {
    rx_rec(&p->body, head, data, len);
    p->n++;
}
void rx_patch_title(rx_patch *p, const char *title) { rx_patch_op(p, "title", title, rx_line_len(title, 80)); }
void rx_patch_badge(rx_patch *p, int n) { rx_buf_fmt(&p->body, "badge %d 0\n", n); p->n++; }
void rx_patch_message(rx_patch *p, const char *text) { rx_patch_op(p, "message", text, rx_line_len(text, 1000)); }
void rx_patch_state(rx_patch *p, const char *state, const char *text, const char *keys) {
    size_t tn = text ? rx_line_len(text, 1000) : 0, kn = keys ? strlen(keys) : 0;
    rx_buf_fmt(&p->body, "state %s %zu %zu\n", state, tn, tn + kn);
    if (tn) rx_buf_add(&p->body, text, tn);
    if (kn) rx_buf_add(&p->body, keys, kn);
    p->n++;
}
void rx_patch_busy(rx_patch *p, int on) { rx_buf_fmt(&p->body, "busy %d 0\n", on ? 1 : 0); p->n++; }
static void rx_patch_children(rx_patch *p, const char *op, const char *parent, size_t plen, const rx_buf *records, int n, int more) {
    size_t rn = records ? records->len : 0;
    rx_buf_fmt(&p->body, "%s %zu %d %d %zu\n", op, plen, n, more ? 1 : 0, plen + rn);
    if (plen) rx_buf_add(&p->body, parent, plen);
    if (rn) rx_buf_add(&p->body, records->data, rn);
    p->nodes += n;
    p->n++;
}
void rx_patch_kids(rx_patch *p, const char *parent, size_t plen, const rx_buf *records, int n, int more) {
    rx_patch_children(p, "kids", parent, plen, records, n, more);
}
void rx_patch_add(rx_patch *p, const char *parent, size_t plen, const rx_buf *records, int n, int more) {
    rx_patch_children(p, "add", parent, plen, records, n, more);
}
void rx_patch_set(rx_patch *p, const rx_buf *record) { rx_patch_op(p, "set", record->data, record->len); }
void rx_patch_del(rx_patch *p, const char *id, size_t len) { rx_patch_op(p, "del", id, len); }
void rx_patch_expand(rx_patch *p, const char *id, size_t len) { rx_patch_op(p, "expand", id, len); }
void rx_patch_collapse(rx_patch *p, const char *id, size_t len) { rx_patch_op(p, "collapse", id, len); }
void rx_patch_raw(rx_patch *p, const char *head, const void *data, size_t len) { rx_patch_op(p, head, data, len); }
void rx_patch_free(rx_patch *p) { rx_buf_free(&p->body); p->n = 0; p->nodes = 0; }
void rx_patch_action(rx_patch *p, const char *key, const char *icon, const char *where, const char *title) {
    rx_buf h = {NULL, 0, 0};
    rx_buf_fmt(&h, "action %s %s %s", key, icon && *icon ? icon : "-", where);
    rx_patch_op(p, h.data, title, rx_line_len(title, 200));
    rx_buf_free(&h);
}
void rx_patch_select_mode(rx_patch *p, int multi) { rx_buf_fmt(&p->body, "select-mode %s 0\n", multi ? "multi" : "single"); p->n++; }
void rx_x_inline(rx_buf *x, const char *keys) { rx_rec(x, "inline", keys, strlen(keys)); }
void rx_x_menu(rx_buf *x, const char *keys) { rx_rec(x, "menu", keys, strlen(keys)); }
int rx_vreveal(rx_ext *ext, int64_t view, const char *id, size_t len, const char *how) {
    char line[160];
    snprintf(line, sizeof line, "vreveal %lld %s %zu", (long long)view, how && *how ? how : "-", len);
    return rx_request(ext, line, id, len, NULL, NULL);
}

/* vkids VIEW REV LEN, vmore VIEW REV HAVE LEN (f[0] the id) with the node id as body */
static void rx_children_answer(rx_ext *ext, const rx_handlers *h, void *user, char **f, int n,
                               const unsigned char *body, size_t body_len) {
    rx_buf line = {NULL, 0, 0};
    int more = strcmp(f[1], "vmore") == 0;
    if (!h->view_children || n < (more ? 6 : 5)) {
        rx_buf_fmt(&line, "error %s unknown no such request", f[0]);
        rx_write(line.data, NULL, 0);
        rx_buf_free(&line);
        return;
    }
    rx_children_ask req;
    req.id = atoll(f[0]), req.view = atoll(f[2]), req.rev = atoll(f[3]), req.have = more ? atoll(f[4]) : -1;
    req.node = body ? body : (const unsigned char *)"", req.node_len = body_len;
    rx_patch p;
    memset(&p, 0, sizeof p);
    int ok = h->view_children(ext, &req, &p, user);
    if (ok && !p.fail) {
        rx_buf b = {NULL, 0, 0};
        rx_buf_fmt(&b, "%d\n", p.n);
        if (p.body.len) rx_buf_add(&b, p.body.data, p.body.len);
        rx_buf_fmt(&line, "ok %s %zu", f[0], b.len);
        rx_write(line.data, b.data, b.len);
        rx_buf_free(&b);
    } else {
        rx_buf_fmt(&line, "error %s %s %s", f[0], ok ? "refused" : "unknown", ok ? p.fail : "no such request");
        rx_write(line.data, NULL, 0);
    }
    rx_patch_free(&p);
    rx_buf_free(&line);
}

int64_t rx_view_patch(rx_ext *ext, int64_t view, const rx_patch *p) {
    if (!rx_has(ext, "view-tree")) {
        snprintf(ext->error, sizeof ext->error, "unknown view-tree feature was not agreed");
        return -1;
    }
    if (p->nodes > 100000) {
        snprintf(ext->error, sizeof ext->error, "toobig more than 100000 nodes in one patch");
        return -1;
    }
    rx_buf body = {NULL, 0, 0}, f = {NULL, 0, 0};
    rx_buf_fmt(&body, "%d\n", p->n);
    if (p->body.len) rx_buf_add(&body, p->body.data, p->body.len);
    char line[96];
    snprintf(line, sizeof line, "vpatch %lld %zu", (long long)view, body.len);
    int r = rx_request(ext, line, body.data, body.len, &f, NULL);
    int64_t rev = r < 0 ? -1 : (f.data ? atoll(f.data) : 0);
    rx_buf_free(&body);
    rx_buf_free(&f);
    return rev;
}

int64_t rx_vshow(rx_ext *ext, const char *name, int focus) {
    rx_buf line = {NULL, 0, 0}, f = {NULL, 0, 0};
    rx_buf_fmt(&line, "vshow %s %s", name, focus ? "focus" : "-");
    int r = rx_request(ext, line.data, NULL, 0, &f, NULL);
    int64_t view = r < 0 ? -1 : (f.data ? atoll(f.data) : 0);
    rx_buf_free(&line);
    rx_buf_free(&f);
    return view;
}

void rx_patch_log(rx_patch *p, int n, const char *const *sevs, const char *const *texts) {
    rx_buf d = {NULL, 0, 0};
    for (int i = 0; i < n; i++) {
        const char *sev = sevs && sevs[i] && *sevs[i] ? sevs[i] : "-";
        rx_buf_add(&d, sev, strlen(sev));
        rx_buf_add(&d, " ", 1);
        const char *t = texts[i] ? texts[i] : "";
        for (; *t; t++) rx_buf_add(&d, *t == '\n' ? " " : t, 1);
        rx_buf_add(&d, "\n", 1);
    }
    char head[48];
    snprintf(head, sizeof head, "log %d", n);
    rx_patch_op(p, head, d.data, d.len);
    rx_buf_free(&d);
}
void rx_patch_clear(rx_patch *p) { rx_patch_op(p, "clear", NULL, 0); }

int64_t rx_image(rx_ext *ext, const void *data, size_t len) {
    rx_buf f = {NULL, 0, 0};
    char line[64];
    snprintf(line, sizeof line, "vimage %zu", len);
    int r = rx_request(ext, line, data, len, &f, NULL);
    int64_t img = r < 0 ? -1 : (f.data ? atoll(f.data) : 0);
    rx_buf_free(&f);
    return img;
}
int rx_unimage(rx_ext *ext, int64_t image) {
    char line[64];
    snprintf(line, sizeof line, "unimage %lld", (long long)image);
    return rx_request(ext, line, NULL, 0, NULL, NULL);
}

int rx_unimage_async(rx_ext *ext, int64_t image) {
    if (ext->gone || !rx_has(ext, "view-images")) return -1;
    if (ext->next_id >= 2147483647) ext->next_id = 0;
    char line[96];
    snprintf(line, sizeof line, "%lld unimage %lld", (long long)++ext->next_id, (long long)image);
    rx_write(line, NULL, 0);
    return 0;
}
void rx_pic_image(rx_buf *picture, double x, double y, double w, double h, int64_t image) {
    rx_buf_fmt(picture, "image %g %g %g %g %lld\n", x, y, w < 0 ? 0 : w, h < 0 ? 0 : h, (long long)image);
}

int64_t rx_vtab(rx_ext *ext, const char *kind, const char *title) {
    rx_buf line = {NULL, 0, 0}, f = {NULL, 0, 0};
    rx_buf_fmt(&line, "vtab %s ", kind);
    rx_buf_add(&line, title, rx_line_len(title, 80));
    rx_buf_add(&line, "", 1);
    line.len--;
    int r = rx_request(ext, line.data, NULL, 0, &f, NULL);
    int64_t view = r < 0 ? -1 : (f.data ? atoll(f.data) : 0);
    rx_buf_free(&line);
    rx_buf_free(&f);
    return view;
}

void rx_note_init(rx_note *n, const char *msg) {
    memset(n, 0, sizeof *n);
    size_t len = strlen(msg);
    if (len > 2000) {
        len = 2000;
        while (len > 0 && ((unsigned char)msg[len] & 0xc0) == 0x80) len--;
    }
    rx_buf_add(&n->msg, msg, len);
}
void rx_note_detail(rx_note *n, const char *text) { rx_rec(&n->extra, "detail", text, rx_line_len(text, 1000)); }
void rx_note_sticky(rx_note *n) { rx_rec(&n->extra, "sticky", NULL, 0); }
void rx_note_button(rx_note *n, const char *title, const char *command, const void *args, size_t len) {
    if (n->buttons >= 3) return;
    n->buttons++;
    rx_rec(&n->extra, "button", title, rx_line_len(title, 60));
    if (command) {
        rx_buf h = {NULL, 0, 0};
        rx_buf_fmt(&h, "command %s", command);
        rx_rec(&n->extra, h.data, args, len);
        rx_buf_free(&h);
    }
}
/* ---- tables, forms, doc views, quick input */

void rx_patch_columns(rx_patch *p, int n, const rx_column *cols) {
    rx_buf d = {NULL, 0, 0};
    if (n > 16) n = 16;
    for (int i = 0; i < n; i++) {
        size_t tn = rx_line_len(cols[i].title, 200);
        rx_buf_fmt(&d, "%s %d %s %zu\n", cols[i].right ? "right" : "left", cols[i].min, cols[i].sort ? "sort" : "-", tn);
        rx_buf_add(&d, cols[i].title, tn);
    }
    char head[32];
    snprintf(head, sizeof head, "columns %d", n);
    rx_patch_op(p, head, d.data, d.len);
    rx_buf_free(&d);
}
void rx_patch_sorted(rx_patch *p, int col, int desc) {
    if (col <= 0) rx_buf_fmt(&p->body, "sorted - - 0\n");
    else rx_buf_fmt(&p->body, "sorted %d %s 0\n", col, desc ? "desc" : "asc");
    p->n++;
}
void rx_x_cell(rx_buf *x, const char *text) { rx_rec(x, "cell", text, rx_line_len(text, 1 << 20)); }
void rx_field_rec(rx_buf *out, const char *key, const char *type, int row, int weight, const char *label,
                  const rx_buf *extra) {
    size_t ln = rx_line_len(label, 200), xn = extra ? extra->len : 0;
    rx_buf_fmt(out, "%s %s %d %d %zu %zu\n", key, type, row, weight, ln, xn);
    rx_buf_add(out, label, ln);
    if (xn) rx_buf_add(out, extra->data, xn);
}
void rx_fx_value(rx_buf *x, int64_t seq, const char *value) {
    char head[48];
    if (seq < 0) snprintf(head, sizeof head, "value -");
    else snprintf(head, sizeof head, "value %lld", (long long)seq);
    rx_rec(x, head, value, strlen(value));
}
void rx_fx_placeholder(rx_buf *x, const char *text) { rx_rec(x, "placeholder", text, rx_line_len(text, 200)); }
void rx_fx_options(rx_buf *x, const char *lines) { rx_rec(x, "options", lines, strlen(lines)); }
void rx_fx_msg(rx_buf *x, const char *severity, const char *text) {
    char head[48];
    snprintf(head, sizeof head, "msg %s", severity);
    rx_rec(x, head, text, rx_line_len(text, 1000));
}
void rx_patch_form(rx_patch *p, const rx_buf *records, int n) {
    char head[32];
    snprintf(head, sizeof head, "form %d", n);
    rx_patch_op(p, head, records ? records->data : NULL, records ? records->len : 0);
}
void rx_patch_field(rx_patch *p, const rx_buf *record) { rx_patch_op(p, "field", record->data, record->len); }
void rx_patch_doc(rx_patch *p, const char *markdown, size_t len) { rx_patch_op(p, "doc", markdown, len); }
void rx_patch_base(rx_patch *p, const char *folder) { rx_patch_op(p, "base", folder, rx_line_len(folder, 4096)); }
void rx_patch_follow(rx_patch *p, int64_t doc) { rx_buf_fmt(&p->body, "follow %lld 0\n", (long long)doc); p->n++; }
void rx_qs_title(rx_buf *b, const char *title) { rx_rec(b, "title", title, rx_line_len(title, 200)); }
void rx_qs_mode(rx_buf *b, const char *mode) { rx_buf_fmt(b, "mode %s 0\n", mode); }
void rx_qs_step(rx_buf *b, int n, int of) { rx_buf_fmt(b, "step %d %d 0\n", n, of); }
void rx_qs_placeholder(rx_buf *b, const char *text) { rx_rec(b, "placeholder", text, rx_line_len(text, 200)); }
void rx_qs_prompt(rx_buf *b, const char *text) { rx_rec(b, "prompt", text, rx_line_len(text, 1000)); }
void rx_qs_value(rx_buf *b, int64_t seq, const char *text) {
    char head[48];
    if (seq < 0) snprintf(head, sizeof head, "value -");
    else snprintf(head, sizeof head, "value %lld", (long long)seq);
    rx_rec(b, head, text, rx_line_len(text, 65536));
}
void rx_qs_flags(rx_buf *b, const char *flags) { rx_buf_fmt(b, "flags %s 0\n", flags[0] ? flags : "-"); }
void rx_qs_msg(rx_buf *b, int64_t seq, const char *severity, const char *text) {
    char head[64];
    snprintf(head, sizeof head, "msg %lld %s", (long long)seq, severity);
    rx_rec(b, head, text, rx_line_len(text, 1000));
}
void rx_qs_active(rx_buf *b, const char *id) { rx_rec(b, "active", id, strlen(id)); }
void rx_qi_rec(rx_buf *out, const char *id, const char *label, int sep, const rx_buf *extra) {
    size_t in = strlen(id), ln = rx_line_len(label, 1000), xn = extra ? extra->len : 0;
    rx_buf_fmt(out, "%zu %zu %zu %s\n", in, ln, xn, sep ? "sep" : "item");
    rx_buf_add(out, id, in);
    rx_buf_add(out, label, ln);
    if (xn) rx_buf_add(out, extra->data, xn);
}
void rx_qs_items(rx_buf *b, const rx_buf *records, int n) {
    char head[32];
    snprintf(head, sizeof head, "items %d", n);
    rx_rec(b, head, records ? records->data : NULL, records ? records->len : 0);
}
int64_t rx_quick(rx_ext *ext, const rx_buf *step) {
    rx_buf f = {NULL, 0, 0};
    char line[64];
    snprintf(line, sizeof line, "quick %zu", step->len);
    int r = rx_request(ext, line, step->data, step->len, &f, NULL);
    int64_t q = r < 0 ? -1 : (f.data ? atoll(f.data) : 0);
    rx_buf_free(&f);
    return q;
}
int rx_quick_set(rx_ext *ext, int64_t q, const rx_buf *step) {
    char line[96];
    snprintf(line, sizeof line, "quick-set %lld %zu", (long long)q, step->len);
    return rx_request(ext, line, step->data, step->len, NULL, NULL);
}
int rx_quick_close(rx_ext *ext, int64_t q) {
    char line[64];
    snprintf(line, sizeof line, "quick-close %lld", (long long)q);
    return rx_request(ext, line, NULL, 0, NULL, NULL);
}

void rx_note_free(rx_note *n) { rx_buf_free(&n->msg); rx_buf_free(&n->extra); n->buttons = 0; }
static void rx_note_body(const rx_note *n, rx_buf *b) {
    rx_buf_fmt(b, "%zu %zu\n", n->msg.len, n->extra.len);
    if (n->msg.len) rx_buf_add(b, n->msg.data, n->msg.len);
    if (n->extra.len) rx_buf_add(b, n->extra.data, n->extra.len);
}
int64_t rx_notify(rx_ext *ext, const char *severity, const rx_note *n) {
    rx_buf body = {NULL, 0, 0}, f = {NULL, 0, 0};
    rx_note_body(n, &body);
    char line[96];
    snprintf(line, sizeof line, "notify %s %zu", severity, body.len);
    int r = rx_request(ext, line, body.data, body.len, &f, NULL);
    int64_t note = r < 0 ? -1 : (f.data ? atoll(f.data) : 0);
    rx_buf_free(&body);
    rx_buf_free(&f);
    return note;
}
int rx_notify_set(rx_ext *ext, int64_t note, const rx_note *n) {
    rx_buf body = {NULL, 0, 0};
    rx_note_body(n, &body);
    char line[96];
    snprintf(line, sizeof line, "notify-set %lld %zu", (long long)note, body.len);
    int r = rx_request(ext, line, body.data, body.len, NULL, NULL);
    rx_buf_free(&body);
    return r;
}
int rx_unnotify(rx_ext *ext, int64_t note) {
    char line[64];
    snprintf(line, sizeof line, "unnotify %lld", (long long)note);
    return rx_request(ext, line, NULL, 0, NULL, NULL);
}
int64_t rx_progress(rx_ext *ext, const char *where, int cancel, const char *title) {
    size_t tn = rx_line_len(title, 200);
    if (tn == 0) title = "\xe2\x80\xa6", tn = 3;
    rx_buf f = {NULL, 0, 0};
    char line[160];
    snprintf(line, sizeof line, "progress %s %s %zu", where, cancel ? "cancel" : "-", tn);
    int r = rx_request(ext, line, title, tn, &f, NULL);
    int64_t p = r < 0 ? -1 : (f.data ? atoll(f.data) : 0);
    rx_buf_free(&f);
    return p;
}
int rx_progress_set(rx_ext *ext, int64_t progress, int percent, const char *msg) {
    char pct[16], line[96];
    if (percent < 0) snprintf(pct, sizeof pct, "-");
    else snprintf(pct, sizeof pct, "%d", percent > 100 ? 100 : percent);
    size_t mn = msg ? rx_line_len(msg, 200) : 0;
    snprintf(line, sizeof line, "progress-set %lld %s %zu", (long long)progress, pct, mn);
    return rx_request(ext, line, msg, mn, NULL, NULL);
}
int rx_progress_end(rx_ext *ext, int64_t progress) {
    char line[64];
    snprintf(line, sizeof line, "progress-end %lld", (long long)progress);
    return rx_request(ext, line, NULL, 0, NULL, NULL);
}

/* a retained view's event (or open) to its handler; 0 when it is a 12a picture event */
static int rx_retained_dispatch(rx_ext *ext, const rx_handlers *h, void *user, char **f, int n,
                                const unsigned char *body, size_t body_len) {
    if (n < 4) return 0;
    const char *k = f[3];
    if (strcmp(k, "open") == 0) {
        if (n < 7 || !h->view_open) return n >= 7;
        rx_view_open o;
        o.view = atoll(f[2]), o.kind = f[4], o.place = f[5], o.name = f[6];
        h->view_open(ext, &o, user);
        return 1;
    }
    static const char *picture[] = {"size", "metrics", "hover", "down", "drag", "up", "click", "leave", "wheel",
                                    "key", "text", "paste", "selectall", "dblclick", NULL};
    for (int i = 0; picture[i]; i++)
        if (strcmp(k, picture[i]) == 0) return 0;
    if (!h->view_event) return strcmp(k, "closed") != 0;
    rx_retained_event e;
    memset(&e, 0, sizeof e);
    e.kind = k, e.argc = n - 4, e.argv = f + 4, e.body = body, e.body_len = body_len, e.key = "";
    if (n > 4) e.on = strcmp(f[4], "1") == 0, e.rev = atoll(f[4]);
    if (strcmp(k, "check") == 0 && n > 5) e.on = strcmp(f[5], "1") == 0;
    if ((strcmp(k, "action") == 0 || strcmp(k, "discarded") == 0 || strcmp(k, "change") == 0 ||
         strcmp(k, "press") == 0 || strcmp(k, "submit") == 0) && n > 5)
        e.key = f[5];
    if (body && body_len) {
        e.id = body, e.id_len = body_len;
        if (strcmp(k, "select") == 0) {
            const unsigned char *nl = (const unsigned char *)memchr(body, '\n', body_len);
            if (nl) e.id_len = (size_t)(nl - body);
        }
    }
    h->view_event(ext, atoll(f[2]), &e, user);
    return strcmp(k, "closed") != 0;    /* a closed tab is also a picture's event */
}

/* ---- notebooks */

void rx_nb_begin(rx_buf *out, int cells, const void *meta, size_t meta_len) {
    rx_buf_fmt(out, "%d %zu\n", cells, meta_len);
    rx_buf_add(out, meta, meta_len);
}

void rx_nb_cell(rx_buf *out, const char *kind, const char *lang, int64_t count, const void *source,
                size_t source_len, const void *meta, size_t meta_len, int outputs) {
    int markup = strcmp(kind, "markup") == 0;
    rx_buf_fmt(out, "%s ", markup ? "markup" : "code");
    if (markup || !lang || !*lang) rx_buf_str(out, "-");
    else for (const char *p = lang; *p; p++) rx_buf_add(out, *p == ' ' ? "_" : p, 1);
    if (count >= 0) rx_buf_fmt(out, " %lld", (long long)count);
    else rx_buf_str(out, " -");
    rx_buf_fmt(out, " %zu %zu %d\n", source_len, meta_len, outputs);
    rx_buf_add(out, source, source_len);
    rx_buf_add(out, meta, meta_len);
}

static int rx_nb_token(const char *t, size_t n) {
    if (!n || n > 64) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = t[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || strchr("_.+/-", c))) return 0;
    }
    return 1;
}

void rx_nb_output(rx_buf *out, const char *kind, const char *id, const void *keep, size_t keep_len, int items) {
    rx_buf_fmt(out, "%s %s %zu %d\n", kind, id && rx_nb_token(id, strlen(id)) ? id : "-", keep_len, items);
    rx_buf_add(out, keep, keep_len);
}

void rx_nb_item(rx_buf *out, const char *type, int w, int h, const void *data, size_t len) {
    rx_buf_fmt(out, "%s %d %d %zu\n", type && rx_nb_token(type, strlen(type)) ? type : "text", w, h, len);
    rx_buf_add(out, data, len);
}

/* rx_nb_fields: the next line of the reader as up to 8 fields copied into buf */
static int rx_nb_fields(rx_nb_reader *r, char *buf, size_t cap, char **f) {
    const unsigned char *nl = r->p;
    while (nl < r->end && *nl != '\n') nl++;
    if (nl >= r->end || (size_t)(nl - r->p) >= cap) return -1;
    memcpy(buf, r->p, (size_t)(nl - r->p));
    buf[nl - r->p] = 0;
    r->p = nl + 1;
    int n = 0;
    char *p = buf;
    while (*p && n < 8) {
        while (*p == ' ') p++;
        if (!*p) break;
        f[n++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
    }
    return n;
}

static int rx_nb_size(const char *s, long long max, size_t *out) {
    if (!*s || strlen(s) > 18) return 0;
    long long v = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return 0;
        v = v * 10 + (*s - '0');
    }
    if (v > max) return 0;
    *out = (size_t)v;
    return 1;
}

static const unsigned char *rx_nb_take(rx_nb_reader *r, size_t n) {
    if ((size_t)(r->end - r->p) < n) return NULL;
    const unsigned char *b = r->p;
    r->p += n;
    return b;
}

int rx_nb_read_begin(rx_nb_reader *r, const unsigned char *body, size_t len) {
    char buf[128], *f[8];
    size_t n, ml;
    r->p = body;
    r->end = body + len;
    if (rx_nb_fields(r, buf, sizeof buf, f) != 2 || !rx_nb_size(f[0], 5000, &n) ||
        !rx_nb_size(f[1], (long long)64 << 20, &ml) || !(r->meta = rx_nb_take(r, ml)))
        return 0;
    r->cells = (int)n;
    r->meta_len = ml;
    return 1;
}

int rx_nb_read_cell(rx_nb_reader *r, rx_nb_cell_rec *c) {
    char buf[256], *f[8];
    size_t sl, ml, no;
    if (rx_nb_fields(r, buf, sizeof buf, f) != 6 || (strcmp(f[0], "code") && strcmp(f[0], "markup")) ||
        strlen(f[1]) >= sizeof c->lang || !rx_nb_size(f[3], 4 << 20, &sl) ||
        !rx_nb_size(f[4], (long long)64 << 20, &ml) || !rx_nb_size(f[5], 1000, &no))
        return 0;
    snprintf(c->kind, sizeof c->kind, "%s", f[0]);
    snprintf(c->lang, sizeof c->lang, "%s", f[1]);
    c->count = strcmp(f[2], "-") == 0 ? -1 : atoll(f[2]);
    if (!(c->source = rx_nb_take(r, sl)) || !(c->meta = rx_nb_take(r, ml))) return 0;
    c->source_len = sl;
    c->meta_len = ml;
    c->outputs = (int)no;
    return 1;
}

int rx_nb_read_output(rx_nb_reader *r, rx_nb_output_rec *o) {
    char buf[256], *f[8];
    size_t kl, ni;
    if (rx_nb_fields(r, buf, sizeof buf, f) != 4 || strlen(f[0]) >= sizeof o->kind ||
        (strcmp(f[1], "-") && !rx_nb_token(f[1], strlen(f[1]))) ||
        !rx_nb_size(f[2], (long long)64 << 20, &kl) || !rx_nb_size(f[3], 32, &ni))
        return 0;
    snprintf(o->kind, sizeof o->kind, "%s", f[0]);
    snprintf(o->id, sizeof o->id, "%s", strcmp(f[1], "-") ? f[1] : "");
    if (!(o->keep = rx_nb_take(r, kl))) return 0;
    o->keep_len = kl;
    o->items = (int)ni;
    return 1;
}

int rx_nb_read_item(rx_nb_reader *r, rx_nb_item_rec *it) {
    char buf[256], *f[8];
    size_t w, h, len;
    if (rx_nb_fields(r, buf, sizeof buf, f) != 4 || !rx_nb_token(f[0], strlen(f[0])) ||
        !rx_nb_size(f[1], 65536, &w) || !rx_nb_size(f[2], 65536, &h) || !rx_nb_size(f[3], 16 << 20, &len))
        return 0;
    snprintf(it->type, sizeof it->type, "%s", f[0]);
    it->w = (int)w;
    it->h = (int)h;
    if (!(it->data = rx_nb_take(r, len))) return 0;
    it->len = len;
    return 1;
}

static int rx_nb_agreed(rx_ext *ext) {
    if (rx_has(ext, "notebooks")) return 1;
    snprintf(ext->error, sizeof ext->error, "unknown notebooks was not agreed");
    return 0;
}

int rx_nb_state(rx_ext *ext, int64_t nb, int64_t cell, int64_t run, const char *state, int64_t count) {
    if (!rx_nb_agreed(ext)) return -1;
    char line[160], c[32] = "-";
    if (count >= 0) snprintf(c, sizeof c, "%lld", (long long)count);
    snprintf(line, sizeof line, "nb-state %lld %lld %lld %s %s", (long long)nb, (long long)cell, (long long)run, state, c);
    return rx_request(ext, line, NULL, 0, NULL, NULL);
}

int rx_nb_output_send(rx_ext *ext, int64_t nb, int64_t cell, int64_t run, const char *op, const char *target,
                      const void *body, size_t len) {
    if (!rx_nb_agreed(ext)) return -1;
    char line[256];
    snprintf(line, sizeof line, "nb-output %lld %lld %lld %s %s %zu", (long long)nb, (long long)cell, (long long)run,
             op, target && *target ? target : "-", len);
    return rx_request(ext, line, body, len, NULL, NULL);
}

int rx_nb_stream(rx_ext *ext, int64_t nb, int64_t cell, int64_t run, int is_stderr, const void *data, size_t len) {
    if (!rx_nb_agreed(ext)) return -1;
    const unsigned char *p = (const unsigned char *)data;
    while (len) {
        size_t n = len < 65536 ? len : 65536;
        while (n < len && n > 0 && (p[n] & 0xc0) == 0x80) n--;
        if (!n) n = len < 65536 ? len : 65536;
        char line[160];
        snprintf(line, sizeof line, "nb-stream %lld %lld %lld %s %zu", (long long)nb, (long long)cell, (long long)run,
                 is_stderr ? "stderr" : "stdout", n);
        if (rx_request(ext, line, p, n, NULL, NULL) < 0) return -1;
        p += n;
        len -= n;
    }
    return 0;
}

int rx_nb_kernel_label(rx_ext *ext, int64_t nb, const char *state, const char *label) {
    if (!rx_nb_agreed(ext)) return -1;
    char text[201], line[96];
    size_t n = 0;
    for (const char *p = label; *p && n < 200; p++) text[n++] = (unsigned char)*p < 32 ? ' ' : *p;
    while (n > 0 && n < strlen(label) && ((unsigned char)text[n] & 0xc0) == 0x80) n--;
    text[n] = 0;
    snprintf(line, sizeof line, "nb-kernel-label %lld %s %zu", (long long)nb, state, n);
    return rx_request(ext, line, text, n, NULL, NULL);
}

void rx_command_body(rx_buf *out, const char *program, const char *dir, const char *const *args, int argc,
                     const char *const *env, int envc) {
    rx_buf_fmt(out, "%zu %zu %d %d\n", strlen(program), strlen(dir), argc, envc);
    rx_buf_str(out, program);
    rx_buf_str(out, dir);
    for (int i = 0; i < argc + envc; i++) {
        const char *a = i < argc ? args[i] : env[i - argc];
        rx_buf_fmt(out, "%zu\n", strlen(a));
        rx_buf_str(out, a);
    }
}

int64_t rx_nb_exec(rx_ext *ext, int64_t nb, const char *program, const char *dir, const char *const *args, int argc,
                   const char *const *env, int envc) {
    if (!rx_nb_agreed(ext)) return -1;
    rx_buf b = {NULL, 0, 0};
    rx_command_body(&b, program, dir ? dir : "", args, argc, env, envc);
    char line[96];
    snprintf(line, sizeof line, "nb-exec %lld %zu", (long long)nb, b.len);
    int64_t v = rx_req_num(ext, line, b.data, b.len);
    rx_buf_free(&b);
    return v;
}

int rx_nb_exec_signal(rx_ext *ext, int64_t exec, const char *signal) {
    if (!rx_nb_agreed(ext)) return -1;
    char line[96];
    snprintf(line, sizeof line, "nb-exec-signal %lld %s", (long long)exec, signal);
    return rx_request(ext, line, NULL, 0, NULL, NULL);
}

unsigned char *rx_editor_read(rx_ext *ext, rx_doc cdoc, int64_t offset, size_t max, size_t *len) {
    char line[128];
    if (max > 4194304) max = 4194304;
    snprintf(line, sizeof line, "editor-read %lld %lld %zu", (long long)cdoc, (long long)offset, max);
    rx_buf f = {NULL, 0, 0}, b = {NULL, 0, 0};
    *len = 0;
    if (rx_request(ext, line, NULL, 0, &f, &b) < 0) {
        rx_buf_free(&f);
        rx_buf_free(&b);
        return NULL;
    }
    rx_buf_free(&f);
    unsigned char *out = (unsigned char *)malloc(b.len + 1);
    if (!out) abort();
    if (b.len) memcpy(out, b.data, b.len);
    *len = b.len;
    rx_buf_free(&b);
    return out;
}

const char *rx_editor_source(const rx_ext *ext) { return ext->editor_source ? ext->editor_source : "file"; }

int rx_editor_dirty(rx_ext *ext, rx_doc cdoc, int dirty) {
    char line[96];
    snprintf(line, sizeof line, "editor-dirty %lld %d", (long long)cdoc, dirty ? 1 : 0);
    return rx_request(ext, line, NULL, 0, NULL, NULL);
}

/* editor-open (with its path), editor-save, editor-undo, -redo and -revert: f: ID VERB CDOC ... */
static void rx_editor_answer(rx_ext *ext, const rx_handlers *h, void *user, char **f, int n,
                             const unsigned char *body, size_t len) {
    rx_buf out = {NULL, 0, 0}, line = {NULL, 0, 0};
    const char *why = NULL;
    int known = rx_has(ext, "custom-editors") && h->editor_open && h->editor_save;
    if (known && strcmp(f[1], "editor-open") == 0 && n == 7) {
        char *path = (char *)malloc(len + 1);
        if (!path) abort();
        if (len) memcpy(path, body, len);
        path[len] = 0;
        ext->editor_source = f[5];
        why = h->editor_open(ext, atoll(f[2]), atoll(f[3]), f[4], path, user);
        ext->editor_source = NULL;
        free(path);
    } else if (known && strcmp(f[1], "editor-save") == 0 && n == 5) {
        rx_buf part = {NULL, 0, 0};
        int more = 0;
        why = h->editor_save(ext, atoll(f[2]), f[3], atoll(f[4]), &part, &more, user);
        if (!why && part.len > 4194304) why = "a part over 4 MB";
        if (!why) {
            rx_buf_add(&out, more ? "1\n" : "0\n", 2);
            if (part.len) rx_buf_add(&out, part.data, part.len);
        }
        rx_buf_free(&part);
    } else if (known && n == 3) {
        if (h->editor_cmd) why = h->editor_cmd(ext, atoll(f[2]), f[1] + 7, user);
    } else {
        known = 0;
    }
    if (!known) {
        rx_buf_fmt(&line, "error %s unknown no such request", f[0]);
        rx_write(line.data, NULL, 0);
    } else if (why) {
        rx_buf_fmt(&line, "error %s refused ", f[0]);
        for (const char *p = why; *p && line.len < 1000; p++) rx_buf_add(&line, *p == '\n' ? " " : p, 1);
        rx_write(line.data, NULL, 0);
    } else {
        rx_buf_fmt(&line, "ok %s %zu", f[0], out.len);
        rx_write(line.data, out.data, out.len);
    }
    rx_buf_free(&out);
    rx_buf_free(&line);
}

/* nb-open and nb-save (f: ID VERB NB LEN) with their bodies */
static void rx_nb_answer(rx_ext *ext, const rx_handlers *h, void *user, char **f, const unsigned char *body, size_t len) {
    rx_buf out = {NULL, 0, 0}, line = {NULL, 0, 0};
    const char *why = NULL, *code = "refused";
    int known = rx_has(ext, "notebooks") && (strcmp(f[1], "nb-open") == 0 ? h->nb_open != NULL : h->nb_save != NULL);
    if (known) {
        /* PATH_LEN [DATA_LEN] LF PATH [DATA] */
        const unsigned char *nl = body ? (const unsigned char *)memchr(body, '\n', len) : NULL;
        char head[64] = "";
        size_t pl = 0, dl = 0;
        int is_open = f[1][3] == 'o', ok = 0;
        if (nl && (size_t)(nl - body) < sizeof head) {
            memcpy(head, body, (size_t)(nl - body));
            head[nl - body] = 0;
            char *sp = strchr(head, ' ');
            if (is_open && sp) {
                *sp = 0;
                ok = rx_nb_size(head, (long long)len, &pl) && rx_nb_size(sp + 1, (long long)len, &dl);
            } else if (!is_open && !sp) {
                ok = rx_nb_size(head, (long long)len, &pl);
            }
        }
        size_t at = nl ? (size_t)(nl - body) + 1 : 0;
        if (ok && pl <= len - at && (!is_open || dl == len - at - pl)) {
            char *path = (char *)malloc(pl + 1);
            if (!path) abort();
            memcpy(path, body + at, pl);
            path[pl] = 0;
            if (is_open) {
                rx_nb_open req = {atoll(f[0]), atoll(f[2]), path, body + at + pl, dl};
                why = h->nb_open(ext, &req, &out, user);
            } else {
                rx_nb_save req = {atoll(f[0]), atoll(f[2]), path, body + at + pl, len - at - pl};
                why = h->nb_save(ext, &req, &out, user);
            }
            free(path);
        } else {
            code = "syntax";
            why = "the body cannot be read";
        }
    }
    if (!known) {
        rx_buf_fmt(&line, "error %s unknown no such request", f[0]);
        rx_write(line.data, NULL, 0);
    } else if (why) {
        rx_buf_fmt(&line, "error %s %s ", f[0], code);
        for (const char *p = why; *p && line.len < 1000; p++) rx_buf_add(&line, *p == '\n' ? " " : p, 1);
        rx_write(line.data, NULL, 0);
    } else {
        rx_buf_fmt(&line, "ok %s %zu", f[0], out.len);
        rx_write(line.data, out.data, out.len);
    }
    rx_buf_free(&out);
    rx_buf_free(&line);
}

static void rx_view_dispatch(rx_ext *ext, const rx_handlers *h, void *user, char **f, int n,
                             const unsigned char *body, size_t body_len) {
    if (n < 4) return;
    rx_view_event ev;
    memset(&ev, 0, sizeof ev);
    ev.kind = f[3];
    ev.mods = ev.key = "";
    const char *k = f[3];
    if ((strcmp(k, "down") == 0 || strcmp(k, "up") == 0 || strcmp(k, "click") == 0 || strcmp(k, "drag") == 0 ||
         strcmp(k, "dblclick") == 0) && n > 6)
        ev.mods = f[6];
    else if (strcmp(k, "wheel") == 0 && n > 7)
        ev.mods = f[7];
    else if (strcmp(k, "key") == 0 && n > 5)
        ev.mods = f[4], ev.key = f[5];
    else if (strcmp(k, "text") == 0 || strcmp(k, "paste") == 0)
        ev.text = body ? body : (const unsigned char *)"", ev.text_len = body_len;
    else if (strcmp(k, "focus") == 0 && n > 4)
        ev.on = atoi(f[4]);
    double v[16] = {0};
    for (int i = 4; i < n && i - 4 < 16; i++) v[i - 4] = rx_atof(f[i]);
    if (strcmp(ev.kind, "size") == 0) {
        ev.width = v[0], ev.height = v[1];
    } else if (strcmp(ev.kind, "metrics") == 0) {
        static const char *names[4] = {"small", "normal", "large", "mono"};
        for (int i = 4; i + 2 < n; i += 3)
            for (int k = 0; k < 4; k++)
                if (strcmp(f[i], names[k]) == 0) ev.advance[k] = rx_atof(f[i + 1]), ev.line_height[k] = rx_atof(f[i + 2]);
    } else {
        ev.x = v[0], ev.y = v[1], ev.dy = v[2];
    }
    h->view(ext, atoll(f[2]), &ev, user);
}

int rx_run(const char *features, const rx_handlers *h, void *user) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    static rx_ext ext_s;
    rx_ext *ext = &ext_s;
    memset(ext, 0, sizeof *ext);
    char hello[1100];
    snprintf(hello, sizeof hello, "hello 1%s%s", features && *features ? " " : "", features ? features : "");
    rx_write(hello, NULL, 0);
    for (;;) {
#ifdef RX_IDLE
        if (!ext->queue && !ext->gone && !RX_IDLE(ext)) continue;
#endif
        rx_msg *m = ext->queue;
        if (m) {
            ext->queue = m->next;
            if (!ext->queue) ext->queue_tail = NULL;
        } else if (ext->gone || !(m = rx_read_msg(ext))) {
            return 1;               /* nothing more can be read: the stream may be out of step */
        }
        char *copy = (char *)malloc(strlen(m->line) + 1);
        if (!copy) abort();
        strcpy(copy, m->line);
        char *f[RX_MAXF];
        int n = rx_split(copy, f);
        int done = 0;
        if (n >= 2 && strcmp(f[0], "0") == 0) {
            rx_event ev;
            ev.name = f[1];
            ev.argc = n - 2;
            ev.argv = f + 2;
            const char *rest = strchr(m->line + 2, ' ');
            ev.rest = rest ? rest + 1 : "";
            ev.body = m->body;
            ev.body_len = m->body_len;
            if (strcmp(f[1], "features") == 0) snprintf(ext->features, sizeof ext->features, "%s", ev.rest);
            if (h->event) h->event(ext, &ev, user);
            if (strcmp(f[1], "ready") == 0 && h->ready) {
                h->ready(ext, strcmp(ev.rest, "-") == 0 ? "" : ev.rest, user);
            } else if (strcmp(f[1], "run") == 0 && n >= 4 && (h->invoke || h->command)) {
                const char *dot = strchr(f[2], '.');
                if (h->invoke) {
                    rx_invocation inv;
                    inv.command = f[2];
                    inv.name = dot ? dot + 1 : f[2];
                    inv.doc = atoll(f[3]);
                    inv.from = n == 6 ? f[4] : "";
                    inv.body = m->body ? m->body : (const unsigned char *)"";
                    inv.body_len = m->body_len;
                    h->invoke(ext, &inv, user);
                } else {
                    h->command(ext, dot ? dot + 1 : f[2], atoll(f[3]), user);
                }
            } else if (strcmp(f[1], "view") == 0) {
                if (!rx_retained_dispatch(ext, h, user, f, n, m->body, m->body_len) && h->view)
                    rx_view_dispatch(ext, h, user, f, n, m->body, m->body_len);
                else if (n >= 4 && strcmp(f[3], "focus") == 0 && h->view)   /* a picture's focus too */
                    rx_view_dispatch(ext, h, user, f, n, m->body, m->body_len);
            } else if (strcmp(f[1], "content-closed") == 0 && h->content_closed) {
                h->content_closed(ext, ev.rest, user);
            } else if (strcmp(f[1], "proposal") == 0 && h->proposal) {
                rx_proposal_event pe;
                if (rx_parse_proposal(&ev, &pe)) h->proposal(ext, &pe, user);
            } else if (strcmp(f[1], "agent-stop") == 0 && n == 3 && h->agent_stop) {
                h->agent_stop(ext, f[2], user);
            } else if (strcmp(f[1], "link") == 0 && h->link) {
                int64_t id;
                const char *link;
                size_t len;
                if (rx_parse_link(&ev, &id, &link, &len)) h->link(ext, id, link, len, user);
            } else if (strcmp(f[1], "action") == 0 && h->action) {
                h->action(ext, m->body ? m->body : (const unsigned char *)"", m->body_len, user);
            } else if (strcmp(f[1], "nb-run") == 0 && n == 7 && h->nb_run && rx_has(ext, "notebooks")) {
                rx_nb_run run;
                run.nb = atoll(f[2]);
                run.cell = atoll(f[3]);
                run.run = atoll(f[4]);
                run.lang = f[5];
                run.source = m->body ? m->body : (const unsigned char *)"";
                run.len = m->body_len;
                h->nb_run(ext, &run, user);
            } else if (strcmp(f[1], "cell") == 0 && n == 6 && h->nb_cell && rx_has(ext, "notebooks")) {
                h->nb_cell(ext, atoll(f[2]), atoll(f[3]), atoll(f[4]), atoi(f[5]), user);
            } else if (strcmp(f[1], "cells") == 0 && n >= 3 && h->nb_cells && rx_has(ext, "notebooks")) {
                /* the whole line: a notebook of many cells has more fields than f holds */
                char *p = (char *)ev.rest, *end;
                int64_t nb = strtoll(p, &end, 10);
                size_t cap = strlen(p) / 2 + 1;
                rx_doc *docs = (rx_doc *)malloc(cap * sizeof *docs);
                int k = 0;
                for (p = end; docs && *p; p = end) {
                    rx_doc d = strtoll(p, &end, 10);
                    if (end == p) break;
                    docs[k++] = d;
                }
                if (docs) h->nb_cells(ext, nb, docs, k, user);
                free(docs);
            } else if (strncmp(f[1], "editor-", 7) == 0 && n >= 3 && h->editor_event && rx_has(ext, "custom-editors")) {
                char *path = NULL;
                if (strcmp(f[1], "editor-path") == 0) {
                    path = (char *)malloc(m->body_len + 1);
                    if (!path) abort();
                    if (m->body_len) memcpy(path, m->body, m->body_len);
                    path[m->body_len] = 0;
                }
                h->editor_event(ext, atoll(f[2]), f[1] + 7, path, user);
                free(path);
            } else if (strcmp(f[1], "nb-exec-exit") == 0 && n == 4 && h->nb_exec_exit && rx_has(ext, "notebooks")) {
                h->nb_exec_exit(ext, atoll(f[2]), strcmp(f[3], "killed") == 0 ? -1 : atoi(f[3]), user);
            } else if (strncmp(f[1], "nb-", 3) == 0 && n == 3 && h->nb_event && rx_has(ext, "notebooks") &&
                       (strcmp(f[1], "nb-interrupt") == 0 || strcmp(f[1], "nb-restart") == 0 ||
                        strcmp(f[1], "nb-kernel") == 0 || strcmp(f[1], "nb-closed") == 0)) {
                h->nb_event(ext, f[1] + 3, atoll(f[2]), user);
            } else if (strcmp(f[1], "notify") == 0 && n >= 4 && h->note_event) {
                if (strcmp(f[3], "pressed") == 0 && n >= 5) h->note_event(ext, atoll(f[2]), atoi(f[4]), user);
                else if (strcmp(f[3], "closed") == 0) h->note_event(ext, atoll(f[2]), 0, user);
            } else if (strcmp(f[1], "quick") == 0 && n >= 4 && h->quick_event) {
                h->quick_event(ext, atoll(f[2]), f[3], n - 4, f + 4, m->body, m->body_len, user);
            } else if (strcmp(f[1], "progress") == 0 && n >= 4 && strcmp(f[3], "cancel") == 0 && h->progress_cancel) {
                h->progress_cancel(ext, atoll(f[2]), user);
            } else if (strcmp(f[1], "test-out") == 0 && n == 5 && h->test_out) {
                h->test_out(ext, atoll(f[2]), strcmp(f[3], "err") == 0,
                            m->body ? m->body : (const unsigned char *)"", m->body_len, user);
            } else if (strcmp(f[1], "test-exit") == 0 && n >= 4 && h->test_exit) {
                h->test_exit(ext, atoll(f[2]), rx_event_tail(&ev, 1), user);
            } else if (strcmp(f[1], "test-cancel") == 0 && n == 3 && h->cancel_tests && rx_has(ext, "tests")) {
                h->cancel_tests(ext, atoll(f[2]), user);
            } else if (strcmp(f[1], "bye") == 0) {
                if (h->bye) h->bye(ext, user);
                done = 1;
            }
        } else if (n >= 2 && f[0][0] >= '1' && f[0][0] <= '9' &&
                   (strcmp(f[1], "vkids") == 0 || strcmp(f[1], "vmore") == 0)) {
            rx_children_answer(ext, h, user, f, n, m->body, m->body_len);
        } else if (n >= 3 && f[0][0] >= '1' && f[0][0] <= '9' && strncmp(f[1], "editor-", 7) == 0) {
            rx_editor_answer(ext, h, user, f, n, m->body, m->body_len);
        } else if (n == 4 && f[0][0] >= '1' && f[0][0] <= '9' && rx_host_body(m->line)) {
            rx_nb_answer(ext, h, user, f, m->body, m->body_len);
        } else if (n >= 2 && f[0][0] >= '1' && f[0][0] <= '9' &&
                   (strcmp(f[1], "tests-resolve") == 0 || strcmp(f[1], "test-run") == 0)) {
            rx_test_answer(ext, h, user, f, n, m->body, m->body_len);
        } else if (n >= 2 && f[0][0] >= '1' && f[0][0] <= '9') {
            rx_answer(ext, h, user, f, n, m->body, m->body_len);
        }
        free(copy);
        rx_free_msg(m);
        if (done) return 0;
    }
}

/* ---- protocol 1.1: agents, proposals and tools ---- */

static int rx_sid_ok(const char *s) {
    size_t n = s ? strlen(s) : 0;
    if (n == 0 || n > 128) return 0;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)s[i] < 0x21 || (unsigned char)s[i] > 0x7e) return 0;
    return 1;
}

static size_t rx_cut_len(const char *s, size_t n, size_t max) {
    if (n <= max) return n;
    while (max > 0 && ((unsigned char)s[max] & 0xc0) == 0x80) max--;
    return max;
}

int rx_agent_sessions(rx_ext *ext, const rx_session *s, int n) {
    rx_buf b = {NULL, 0, 0};
    if (n < 0 || n > 1000) {
        snprintf(ext->error, sizeof ext->error, "toobig %d sessions: at most 1000", n);
        return -1;
    }
    rx_buf_fmt(&b, "%d\n", n);
    for (int i = 0; i < n; i++) {
        const char *cwd = s[i].cwd ? s[i].cwd : "";
        int bad = !rx_sid_ok(s[i].id) || strchr(cwd, '\n') ||
                  (*cwd && cwd[0] != '/' && cwd[0] != '\\' && !(cwd[0] && cwd[1] == ':'));
        for (int k = 0; k < i && !bad; k++) bad = strcmp(s[k].id, s[i].id) == 0;
        if (bad) {
            snprintf(ext->error, sizeof ext->error, "syntax session %d: an id of 1 to 128 visible bytes, once; an absolute folder", i + 1);
            rx_buf_free(&b);
            return -1;
        }
        const char *title = s[i].title ? s[i].title : "", *model = s[i].model ? s[i].model : "";
        size_t tl = rx_cut_len(title, strlen(title), 4096), ml = rx_cut_len(model, strlen(model), 4096);
        rx_buf_fmt(&b, "%lld %s %zu %zu %zu %zu\n", (long long)s[i].updated, s[i].state ? s[i].state : "idle",
                   strlen(s[i].id), tl, ml, strlen(cwd));
        rx_buf_str(&b, s[i].id);
        rx_buf_add(&b, title, tl);
        rx_buf_add(&b, model, ml);
        rx_buf_str(&b, cwd);
    }
    char line[64];
    snprintf(line, sizeof line, "agent-sessions %zu", b.len);
    int r = rx_request(ext, line, b.data, b.len, NULL, NULL);
    rx_buf_free(&b);
    return r;
}

int rx_agent_changed(rx_ext *ext, const char *sid, int64_t total, int64_t first) {
    char line[256];
    snprintf(line, sizeof line, "agent-changed %s %lld %lld", sid, (long long)total, (long long)first);
    return rx_request(ext, line, NULL, 0, NULL, NULL);
}

void rx_transcript_add(rx_transcript *t, const char *role, const char *name, const char *text, size_t len,
                       const char *model) {
    name = name ? name : "", model = model ? model : "", text = text ? text : "";
    size_t nl = rx_cut_len(name, strlen(name), 4096), tl = rx_cut_len(text, len, 71680),
           ml = rx_cut_len(model, strlen(model), 4096);
    size_t need = 40 + nl + tl + ml;
    if (t->n >= 2000 || t->size + need > ((size_t)4 << 20) - 32) return;
    rx_buf_fmt(&t->recs, "%s %zu %zu %zu\n", role, nl, tl, ml);
    rx_buf_add(&t->recs, name, nl);
    rx_buf_add(&t->recs, text, tl);
    rx_buf_add(&t->recs, model, ml);
    t->size += need;
    t->n++;
}

void rx_transcript_finish(rx_transcript *t, rx_buf *out) {
    rx_buf_fmt(out, "%lld %d\n", (long long)t->total, t->n);
    rx_buf_add(out, t->recs.data, t->recs.len);
    rx_buf_free(&t->recs);
    t->n = 0, t->size = 0;
}

/* 12g's part of an edit: a hashed group's PRE ("fnv1a64:" and 16 digits) goes as "-", the label and
 * confirm are dropped */
static void rx_wedit_plain(rx_wedit *e) {
    if (e->fail) return;
    rx_wedit_flush(e);
    size_t nh = e->hashes.len / sizeof(size_t);
    if (nh) {
        rx_buf b = {NULL, 0, 0};
        size_t from = 0;
        for (size_t i = 0; i < nh; i++) {
            size_t at;
            memcpy(&at, e->hashes.data + i * sizeof at, sizeof at);
            rx_buf_add(&b, e->body.data + from, at - from);
            rx_buf_add(&b, "-", 1);
            from = at + 24;
        }
        rx_buf_add(&b, e->body.data + from, e->body.len - from);
        rx_buf_free(&e->body);
        e->body = b;
        rx_buf_clear(&e->hashes);
    }
    rx_buf_clear(&e->label);
    e->confirm = 0;
}

int rx_proposal_body(const char *label, const char *sid, rx_wedit *edit, rx_buf *out) {
    rx_buf e = {NULL, 0, 0};
    sid = sid ? sid : "", label = label ? label : "";
    rx_wedit_plain(edit);
    if (rx_wedit_finish_caps(edit, &e, 0, 0, "propose") < 0) {   /* 12g's grammar: file operations fail */
        rx_buf_free(&e);
        return -1;
    }
    rx_buf_fmt(out, "%zu %zu\n%s%s", strlen(label), strlen(sid), label, sid);
    rx_buf_add(out, e.data, e.len);
    rx_buf_free(&e);
    return 0;
}

int64_t rx_propose(rx_ext *ext, const char *label, const char *sid, rx_wedit *edit) {
    rx_buf b = {NULL, 0, 0};
    if (rx_proposal_body(label, sid, edit, &b) < 0) {
        snprintf(ext->error, sizeof ext->error, "%s %s", edit->fail, edit->why);
        rx_buf_free(&b);
        return -1;
    }
    char line[64];
    snprintf(line, sizeof line, "propose %zu", b.len);
    int64_t id = rx_req_num(ext, line, b.data, b.len);
    rx_buf_free(&b);
    return id;
}

int rx_unpropose(rx_ext *ext, int64_t proposal) {
    char line[64];
    snprintf(line, sizeof line, "unpropose %lld", (long long)proposal);
    return rx_request(ext, line, NULL, 0, NULL, NULL);
}

int rx_parse_proposal(const rx_event *ev, rx_proposal_event *out) {
    memset(out, 0, sizeof *out);
    if (strcmp(ev->name, "proposal") != 0 || ev->argc < 3) return 0;
    out->proposal = atoll(ev->argv[0]);
    if (strcmp(ev->argv[1], "done") == 0) {
        if (ev->argc < 4) return 0;
        out->state = "done";
        out->accepted = atoll(ev->argv[2]);
        out->rejected = atoll(ev->argv[3]);
        return 1;
    }
    out->index = atoll(ev->argv[1]);
    out->state = ev->argv[2];
    if (strcmp(out->state, "partial") == 0) {
        if (ev->argc < 5) return 0;
        out->accepted = atoll(ev->argv[3]);
        out->hunks = atoll(ev->argv[4]);
    }
    return strcmp(out->state, "accepted") == 0 || strcmp(out->state, "rejected") == 0 ||
           strcmp(out->state, "stale") == 0 || strcmp(out->state, "partial") == 0;
}

static char *rx_take_str(const unsigned char *b, size_t len, size_t *pos, size_t n) {
    if (n > len - *pos) return NULL;
    char *s = (char *)malloc(n + 1);
    if (!s) abort();
    memcpy(s, b + *pos, n);
    s[n] = 0;
    *pos += n;
    return s;
}

int rx_tools_parse(const unsigned char *body, size_t len, rx_tool **tools, int *n) {
    *tools = NULL, *n = 0;
    size_t pos = 0;
    char head[400];
    long long count = -1;
    const unsigned char *nl = (const unsigned char *)memchr(body, '\n', len);
    if (!nl || nl - body > 20) return -1;
    memcpy(head, body, (size_t)(nl - body));
    head[nl - body] = 0;
    count = atoll(head);
    pos = (size_t)(nl - body) + 1;
    if (count < 0 || count > 100000) return -1;
    rx_tool *v = (rx_tool *)calloc((size_t)(count ? count : 1), sizeof *v);
    if (!v) abort();
    for (long long i = 0; i < count; i++) {
        nl = (const unsigned char *)memchr(body + pos, '\n', len - pos);
        if (!nl || (size_t)(nl - body - pos) >= sizeof head) goto bad;
        memcpy(head, body + pos, (size_t)(nl - body - pos));
        head[nl - body - pos] = 0;
        pos = (size_t)(nl - body) + 1;
        char name[300], flags[8];
        size_t tl, dl, il;
        if (sscanf(head, "%299s %7s %zu %zu %zu", name, flags, &tl, &dl, &il) != 5) goto bad;
        v[i].name = (char *)malloc(strlen(name) + 1);
        if (!v[i].name) abort();
        strcpy(v[i].name, name);
        v[i].readonly = strcmp(flags, "ro") == 0;
        *n = (int)i + 1;
        if (!(v[i].title = rx_take_str(body, len, &pos, tl)) || !(v[i].description = rx_take_str(body, len, &pos, dl)) ||
            !(v[i].input = rx_take_str(body, len, &pos, il)))
            goto bad;
    }
    *tools = v, *n = (int)count;
    return 0;
bad:
    rx_tools_free(v, *n);
    *n = 0;
    return -1;
}

void rx_tools_free(rx_tool *tools, int n) {
    for (int i = 0; tools && i < n; i++) {
        free(tools[i].name), free(tools[i].title), free(tools[i].description), free(tools[i].input);
    }
    free(tools);
}

int rx_tools(rx_ext *ext, rx_tool **tools, int *n) {
    rx_buf body = {NULL, 0, 0};
    *tools = NULL, *n = 0;
    if (rx_request(ext, "tools", NULL, 0, NULL, &body) < 0) return -1;
    int r = rx_tools_parse((const unsigned char *)(body.data ? body.data : ""), body.len, tools, n);
    if (r < 0) snprintf(ext->error, sizeof ext->error, "syntax the tool list cannot be read");
    rx_buf_free(&body);
    return r;
}

int rx_tool_call(rx_ext *ext, const char *tool, const void *input, size_t len, rx_buf *out) {
    char line[512];
    snprintf(line, sizeof line, "tool-call %s %zu", tool, len);
    return rx_request(ext, line, input, len, NULL, out);
}

int rx_agent_ask(rx_ext *ext, const char *sid, const char *prompt, const char *const *options, int n) {
    rx_buf b = {NULL, 0, 0};
    rx_buf_str(&b, prompt);
    for (int i = 0; i < n; i++) {
        rx_buf_add(&b, "\n", 1);
        rx_buf_str(&b, options[i]);
    }
    for (size_t i = 0; i < b.len; i++)
        if (b.data[i] == '\r') b.data[i] = ' ';
    char line[256];
    snprintf(line, sizeof line, "agent-ask %s %zu", sid, b.len);
    int64_t pick = rx_req_num(ext, line, b.data, b.len);
    rx_buf_free(&b);
    return pick > 0 ? (int)pick - 1 : -1;
}

static void rx_answer_ai(rx_ext *ext, const rx_handlers *h, void *user, char **f, int n,
                         const unsigned char *in, size_t in_len, int *ok, rx_buf *body) {
    const char *verb = f[1];
    if (strcmp(verb, "agent-transcript") == 0 && n == 4 && h->agent_transcript && rx_has(ext, "agents") &&
        rx_nums(f, 3, 4)) {
        rx_transcript t;
        memset(&t, 0, sizeof t);
        if ((*ok = h->agent_transcript(ext, f[2], atoll(f[3]), &t, user)) > 0) rx_transcript_finish(&t, body);
        rx_buf_free(&t.recs);
    } else if (strcmp(verb, "tool") == 0 && n == 5 && h->tool && rx_has(ext, "tools")) {
        *ok = h->tool(ext, f[2], f[3], in ? in : (const unsigned char *)"", in_len, body, user);
    } else if (strcmp(verb, "agent-prompt") == 0 && n == 4 && h->agent_prompt && rx_has(ext, "agent-prompts")) {
        rx_buf text = {NULL, 0, 0};
        rx_buf_add(&text, in ? (const char *)in : "", in_len);
        rx_buf_add(&text, "", 1);
        *ok = h->agent_prompt(ext, strcmp(f[2], "-") == 0 ? "" : f[2], text.data, in_len, body, user);
        rx_buf_free(&text);
    }
}

#endif /* RHUNEXT_IMPLEMENTATION */
