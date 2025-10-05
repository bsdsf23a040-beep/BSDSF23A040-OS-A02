/*
 * Programming Assignment 02: lsv1.0.0  (+ -l support + multi-column)
 * Usage examples (CMD):
 *      bin\ls.exe
 *      bin\ls.exe -l
 *      bin\ls.exe -l src  .
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <limits.h>
#include <windows.h>   // console width (Windows)

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

extern int errno;

/* -------- permissions string builder -------- */
static void build_perm_string(mode_t m, char out[11]) {
    out[0] = S_ISDIR(m) ? 'd' :
             S_ISLNK(m) ? 'l' :
             S_ISCHR(m) ? 'c' :
             S_ISBLK(m) ? 'b' :
             S_ISFIFO(m)? 'p' :
             S_ISSOCK(m)? 's' : '-';

    out[1] = (m & S_IRUSR) ? 'r' : '-';
    out[2] = (m & S_IWUSR) ? 'w' : '-';
    out[3] = (m & S_IXUSR) ? 'x' : '-';
    out[4] = (m & S_IRGRP) ? 'r' : '-';
    out[5] = (m & S_IWGRP) ? 'w' : '-';
    out[6] = (m & S_IXGRP) ? 'x' : '-';
    out[7] = (m & S_IROTH) ? 'r' : '-';
    out[8] = (m & S_IWOTH) ? 'w' : '-';
    out[9] = (m & S_IXOTH) ? 'x' : '-';
    out[10] = '\0';
}

/* -------- long listing (single file) -------- */
static void print_long_one(const char *fullpath, const char *name) {
    struct stat st;
    if (lstat(fullpath, &st) == -1) {
        perror(fullpath);
        return;
    }

    char perms[11];
    build_perm_string(st.st_mode, perms);

    struct passwd *pw = getpwuid(st.st_uid);
    struct group  *gr = getgrgid(st.st_gid);

    char tbuf[32];
    struct tm *mtm = localtime(&st.st_mtime);
    strftime(tbuf, sizeof(tbuf), "%b %d %H:%M", mtm);

    printf("%s %2lu %-8s %-8s %8ld %s %s\n",
           perms,
           (unsigned long)st.st_nlink,
           pw ? pw->pw_name : "unknown",
           gr ? gr->gr_name : "unknown",
           (long)st.st_size,
           tbuf,
           name);
}

/* -------- console width + columns printing -------- */
static int get_console_width(void) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int columns = 80; // default fallback
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetConsoleScreenBufferInfo(hStdout, &csbi))
        columns = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    return columns;
}

static void print_columns(char **names, int count) {
    if (count == 0) return;

    int width = get_console_width();
    int maxlen = 0;
    for (int i = 0; i < count; i++) {
        int len = (int)strlen(names[i]);
        if (len > maxlen) maxlen = len;
    }

    int col_width = maxlen + 2;
    int cols = width / col_width;
    if (cols < 1) cols = 1;
    int rows = (count + cols - 1) / cols;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int i = c * rows + r;
            if (i < count)
                printf("%-*s", col_width, names[i]);
        }
        putchar('\n');
    }
}

/* -------- directory listing (with -l or columns) -------- */
static void do_ls(const char *dir, int long_flag) {
    struct dirent *entry;
    DIR *dp = opendir(dir);
    if (dp == NULL) {
        fprintf(stderr, "Cannot open directory: %s\n", dir);
        return;
    }

    char *names[4096];
    int count = 0;

    errno = 0;
    while ((entry = readdir(dp)) != NULL) {
        /* skip hidden like original code */
        if (entry->d_name[0] == '.')
            continue;

        if (long_flag) {
            char full[PATH_MAX];
            int need_slash = (dir[0] && dir[strlen(dir)-1] == '/') ? 0 : 1;
            snprintf(full, sizeof(full), "%s%s%s", dir, need_slash ? "/" : "", entry->d_name);
            print_long_one(full, entry->d_name);
        } else {
            /* collect names for multi-column */
            names[count] = _strdup(entry->d_name);   /* Windows-safe strdup */
            if (!names[count]) { perror("strdup"); break; }
            count++;
            if (count >= (int)(sizeof(names)/sizeof(names[0]))) break;
        }
    }
    if (errno != 0) perror("readdir failed");
    closedir(dp);

    if (!long_flag) {
        print_columns(names, count);
        for (int i = 0; i < count; i++) free(names[i]);
    }
}

int main(int argc, char const *argv[]) {
    int opt, long_flag = 0;

    /* parse -l */
    while ((opt = getopt(argc, (char * const *)argv, "l")) != -1) {
        if (opt == 'l') long_flag = 1;
    }

    if (optind == argc) {
        /* no paths → current dir */
        do_ls(".", long_flag);
    } else {
        for (int i = optind; i < argc; i++) {
            struct stat st;
            if (lstat(argv[i], &st) == -1) {
                perror(argv[i]);
                continue;
            }

            printf("Directory listing of %s : \n", argv[i]);

            if (S_ISDIR(st.st_mode)) {
                do_ls(argv[i], long_flag);
            } else {
                if (long_flag) print_long_one(argv[i], argv[i]);
                else           printf("%s\n", argv[i]);
            }
            puts("");
        }
    }
    return 0;
}
