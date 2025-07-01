# RunML
An organized and testable CITS2002 Project 1 solution.


### Description
https://teaching.csse.uwa.edu.au/units/CITS2002/projects/project1.php


### Highlights
* Test-driven development
* Dynamic memory allocation
* Automatic source file merger
* Typed macro collection
* Visitor-based code translation


### Design

#### Split tokens
```c
// iterate tokens
...
while (true) {
    struct ml_token_data data;
    enum ml_token_type type = ml_token_iterate(..., &data);
    if (type == ML_TOKEN_TYPE_EOF) {
        ...
        break;
    } else if (type == ML_TOKEN_TYPE_ERROR) {
        ...
        break;
    } else {
        ...
    }
}
...
```

#### Analyze syntax
```c
// feed tokens to compiler
struct ml_token_ctx *token = NULL;
struct ml_compile_ctx *compile = NULL;
ml_token_ctx_init_file(&token, ...);
ml_compile_ctx_init(&compile, NULL);
...
enum ml_compile_result result = ml_compile_feed(compile, token);
...
ml_token_ctx_uninit(&token);
ml_compile_ctx_uninit(&compile);
```

#### Code translation
```c
// dispatch visitor events to translated code
static void do_write_compile_data(..., enum ml_compile_visit_event event, ...) {
    ...
    switch (event) {
        case ML_COMPILE_VISIT_EVENT_MAIN_FUNC_SECTION_START:
            ...
        case ML_COMPILE_VISIT_EVENT_MAIN_FUNC_VISIT_ARG:
            ...
        case ML_COMPILE_VISIT_EVENT_MAIN_FUNC_SECTION_END:
            ...
        ...
    }
}

// visit compiled result with callback
void ml_codegen_export_fns(struct ml_compile_ctx *compile, ...) {
    ...
    ml_compile_accept(compile, ..., do_write_compile_data);
    ...
}
```


### Building

| Target     | Purpose            |
|------------|--------------------|
| runml_main | Main executable    |
| runml_test | Test runner        |
| runml_dist | Source file Merger |
