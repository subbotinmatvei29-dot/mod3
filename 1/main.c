#include<stdio.h>
#include<stdlib.h>
#include<sys/wait.h>
#include<unistd.h>
void process_arg(const char *s ){
    char *endptr;
    double x= strtod(s,&endptr);
    if (endptr != s && *endptr == '\0') {
        printf("%g %g\n", x, x*2);
    } else {
        printf("%s\n", s );
    }
}

void process_range(char *argv[], int l, int r) {
    for (int i = l; i <= r; i++) {
        process_arg(argv[i]);
    }
}
int main(int argc, char *argv[]){
    if (argc<2){
        printf("no arguments\n");
        return 0;
    }
    int n =argc-1;
    int mid =n/2;
    pid_t pid=fork();
    
    if (pid==-1){
        perror("ошибка");
        return 1;
    } else if (pid==0){
        process_range(argv,mid+1,argc-1 );
        
    }else {
        process_range(argv,1,mid );
        wait(NULL);
    
    }
    return 0;
}
