/*
 * Programming Assignment 02: lsv1.5.0
 * Features:
 *   -l  : long listing
 *   -t  : sort by modification time (newest first)
 *   -S  : sort by size (largest first)
 *   -r  : reverse order
 *   -a  : show hidden (include . and .. and dotfiles)
 * Default sort: by name (A→Z)
 *
 * Usage (CMD on Windows):
 *   bin\ls.exe
 *   bin\ls.exe -a
 *   bin\ls.exe -l
 *   bin\ls.exe -t
 *   bin\ls.exe -Sr
 *   bin\ls.exe -latr src
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

/* -------------------- global sort mode -------------------- */
typedef enum { SORT_NAME = 0, SORT_TIME, SORT_SIZE } sort_mode_t;
static sort_mode_t g_sort_mode = SORT_NAME;
static int g_reverse   = 0;   /* 0 = normal, 1 = reverse */
static int g_long_flag = 0;   /* from -l */
static int g_all_flag  = 0;   /* from -a : show hidden */

/* -------------------- permissions string -------------------- */
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

/* -------------------- long listing (single) -------------------- */
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

/* -------------------- console width + columns -------------------- */
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

/* -------------------- entry list + sorting -------------------- */
typedef struct {
    char       *name;    /* d_name */
    struct stat st;      /* lstat(fullpath) */
} Entry;

static int cmp_name(const void *a, const void *b) {
    const Entry *x = (const Entry*)a, *y = (const Entry*)b;
    int r = strcmp(x->name, y->name);
    return g_reverse ? -r : r;
}
static int cmp_time(const void *a, const void *b) {
    const Entry *x = (const Entry*)a, *y = (const Entry*)b;
    /* newest first (desc) */
    if (x->st.st_mtime == y->st.st_mtime) {
        int r = strcmp(x->name, y->name);
        return g_reverse ? -r : r;
    }
    long diff = (long)(y->st.st_mtime - x->st.st_mtime);
    int r = (diff > 0) ? 1 : -1;
    return g_reverse ? -r : r;
}
static int cmp_size(const void *a, const void *b) {
    const Entry *x = (const Entry*)a, *y = (const Entry*)b;
    /* largest first (desc) */
    if (x->st.st_size == y->st.st_size) {
        int r = strcmp(x->name, y->name);
        return g_reverse ? -r : r;
    }
    int r = (y->st.st_size > x->st.st_size) ? 1 : -1;
    return g_reverse ? -r : r;
}

static void sort_entries(Entry *arr, int n) {
    switch (g_sort_mode) {
        case SORT_TIME: qsort(arr, n, sizeof(Entry), cmp_time); break;
        case SORT_SIZE: qsort(arr, n, sizeof(Entry), cmp_size); break;
        case SORT_NAME:
        default:        qsort(arr, n, sizeof(Entry), cmp_name); break;
    }
}

/* -------------------- directory listing (sorted) -------------------- */
static void do_ls(const char *dir) {
    DIR *dp = opendir(dir);
    if (!dp) { fprintf(stderr, "Cannot open directory: %s\n", dir); return; }

    Entry items[4096];
    int count = 0;
    errno = 0;

    struct dirent *ent;
    while ((ent = readdir(dp)) != NULL) {
        /* show hidden only if -a is set; otherwise skip dotfiles */
        if (!g_all_flag && ent->d_name[0] == '.') continue;

        /* full path */
        char full[PATH_MAX];
        int need_slash = (dir[0] && dir[strlen(dir)-1] == '/') ? 0 : 1;
        snprintf(full, sizeof(full), "%s%s%s", dir, need_slash?"/":"", ent->d_name);

        /* lstat for sorting + long view */
        if (lstat(full, &items[count].st) == -1) {
            perror(full);
            continue;
        }
        items[count].name = strdup(ent->d_name);
        if (!items[count].name) { perror("strdup"); break; }

        count++;
        if (count >= (int)(sizeof(items)/sizeof(items[0]))) break;
    }
    if (errno) perror("readdir failed");
    closedir(dp);

    /* sort as per flags */
    sort_entries(items, count);

    if (g_long_flag) {
        for (int i = 0; i < count; ++i) {
            char full[PATH_MAX];
            int need_slash = (dir[0] && dir[strlen(dir)-1] == '/') ? 0 : 1;
            snprintf(full, sizeof(full), "%s%s%s", dir, need_slash?"/":"", items[i].name);
            print_long_one(full, items[i].name);
        }
    } else {
        char *names[4096];
        for (int i = 0; i < count; ++i) names[i] = items[i].name;
        print_columns(names, count);
    }

    for (int i = 0; i < count; ++i) free(items[i].name);
}

/* -------------------- main -------------------- */
int main(int argc, char *argv[]) {
    int opt;
    /* parse: l, t, S, r, a */
    while ((opt = getopt(argc, argv, "ltSra")) != -1) {
        switch (opt) {
            case 'l': g_long_flag = 1;       break;
            case 't': g_sort_mode = SORT_TIME; break;
            case 'S': g_sort_mode = SORT_SIZE; break;
            case 'r': g_reverse   = 1;       break;
            case 'a': g_all_flag  = 1;       break;
            default: /* ignore */            break;
        }
    }

    if (optind == argc) {
        do_ls(".");
    } else {
        for (int i = optind; i < argc; ++i) {
            struct stat st;
            if (lstat(argv[i], &st) == -1) { perror(argv[i]); continue; }

            printf("Directory listing of %s : \n", argv[i]);
            if (S_ISDIR(st.st_mode)) {
                do_ls(argv[i]);
            } else {
                if (g_long_flag) print_long_one(argv[i], argv[i]);
                else             printf("%s\n", argv[i]);
            }
            puts("");
        }
    }
    return 0;
}
