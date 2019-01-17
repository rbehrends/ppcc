#ifndef _SINGULAR_GLOBALDEFS_H
#define _SINGULAR_GLOBALDEFS_H

#ifndef PSINGULAR
#define VAR
#define EXTERN_VAR extern
#define STATIC_VAR static
#define INST_VAR
#define EXTERN_INST_VAR extern
#define STATIC_INST_VAR static
#define GLOBAL_VAR
#endif

#define MERGE_TOKENS_AUX(a,b,c) a##b##c
#define MERGE_TOKENS(a,b,c) MERGE_TOKENS_AUX(a,b,c)
#define TEMP_VAR_NAME(name) MERGE_TOKENS(_init_,name,__LINE__)
#define TEMP_TYPE_NAME(name) MERGE_TOKENS(_type_,name,__LINE__)

#define THREAD_INIT(name) \
        static class TEMP_TYPE_NAME(name) { \
        public: \
          TEMP_TYPE_NAME(name)() { name(); } \
        } TEMP_VAR_NAME(name)



#endif // _SINGULAR_GLOBALDEFS_H