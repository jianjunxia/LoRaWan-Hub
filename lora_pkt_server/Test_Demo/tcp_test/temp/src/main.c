#include"main.h"
#include"printf.h"

extern  void printf_test(void); 

int
main(int argc, char const *argv[])
{

    #ifdef _MAIN_
        printf("it's inclde main.h define _MAIN_ 1\n");
    #endif
   
    printf_test();

    /* code */
    return 0;
}

