#include "lobster/stdafx.h"

#include "lobster/compiler.h"

#include "libtcc.h"

namespace lobster {

bool RunC(const char *source,
          const char *object_name,
          string &error,
          const void **imports,
          const char **export_names,
          function<bool (void **)> runf) {
    // Wrap this thing in a unique pointer since the compiled code may
    // throw an exception still.
    auto deleter = [&](TCCState *p) { tcc_delete(p); };
    unique_ptr<TCCState, decltype(deleter)> state(tcc_new(), deleter);
    tcc_set_output_type(state.get(), object_name ? TCC_OUTPUT_OBJ : TCC_OUTPUT_MEMORY);
    tcc_set_error_func(state.get(), &error, [](void *err, const char *msg) {
        // No way to disable warnings individually, so filter them here :)
        //if (strstr(msg, "label at end of compound statement")) return;
        *((string *)err) += msg;
        *((string *)err) += "\n";
    });
    #ifdef _WIN32
        // Need to provide chkstk.
        tcc_set_options(state.get(), "-xa");
        // FIXME replace this by an tcc_add_symbol call?
        auto chkstk_src =
            ".globl __chkstk                                             \n"
            "__chkstk:                                                   \n"
            "    xchg    (%rsp),%rbp     /* store ebp, get ret.addr */   \n"
            "    push    %rbp            /* push ret.addr */             \n"
            "    lea     8(%rsp),%rbp    /* setup frame ptr */           \n"
            "    push    %rcx            /* save ecx */                  \n"
            "    mov     %rbp,%rcx                                       \n"
            "    movslq  %eax,%rax                                       \n"
            "P0:                                                         \n"
            "    sub     $4096,%rcx                                      \n"
            "    test    %rax,(%rcx)                                     \n"
            "    sub     $4096,%rax                                      \n"
            "    cmp     $4096,%rax                                      \n"
            "    jge     P0                                              \n"
            "    sub     %rax,%rcx                                       \n"
            "    test    %rax,(%rcx)                                     \n"
            "    mov     %rsp,%rax                                       \n"
            "    mov     %rcx,%rsp                                       \n"
            "    mov     (%rax),%rcx     /* restore ecx */               \n"
            "    jmp     *8(%rax)                                        \n";
        if (tcc_compile_string(state.get(), chkstk_src) < 0) return false;
        tcc_set_options(state.get(), "-xc");
    #endif
    tcc_add_symbol(state.get(), "memmove", (const void *)memmove);
    tcc_set_options(state.get(), "-nostdlib -Wall");
    while (*imports) {
        auto name = (const char *)(*imports++);
        auto fun = *imports++;
        tcc_add_symbol(state.get(), name, fun);
    }
    if (tcc_compile_string(state.get(), source) < 0) return false;
    if (object_name) {
        return tcc_output_file(state.get(), object_name) == 0;
    } else {
        if (tcc_relocate(state.get(), TCC_RELOCATE_AUTO) < 0) return false;
        vector<void *> exports;
        while (*export_names) {
            exports.push_back(tcc_get_symbol(state.get(), *export_names++));
        }
        return runf(exports.data());
    }
}

}

