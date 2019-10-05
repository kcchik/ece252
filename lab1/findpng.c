#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "crc.h"
#include "zutil.h"
#include "lab_png.h"

int list_dir(char *dirPath){
    int count = 0;
    struct dirent *entry;
    DIR *dp;
    char path[PATH_MAX + 1];

    if (!(dp = opendir(dirPath))){
        perror("opendir");
        return errno;
    }

    while((entry = readdir(dp))!= NULL) {
        strcpy(path, dirPath);
        strcat(path, "/");
        strcat(path, entry->d_name);

        if (entry->d_type == DT_DIR) {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                count += list_dir(path);
            }
        }
        else{
            if (is_png(path)) {
                count++;
                printf("%s\n", path);
            }
        }
    }
    closedir(dp);
    return count;
}

int main (int argc, char **argv)
{
    if (argc < 2) {
        printf("Wrong number of arguments: given %d, expected 1\n", argc - 1);
        return -1;
    }

    int pngCount = list_dir(argv[1]);
    if (pngCount < 1){
        printf("findpng: No PNG file found\n");
    }
    return 0;
}
