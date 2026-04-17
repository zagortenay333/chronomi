#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include "os/fs.h"
#include <pwd.h>

String fs_get_home_dir (Mem *mem) {
    CString home = getenv("HOME");

    if (! home) {
        struct passwd *pw = getpwuid(getuid());
        home = pw->pw_dir;
    }

    if (! home) return (String){};
    return str_copy(mem, str(home));
}

String fs_read_entire_file (Mem *mem, String path, U64 extra_space) {
    tmem_new(tm);

    Auto fd = open(cstr(tm, path), O_RDONLY);
    if (fd < 0) return (String){};

    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return (String){}; }
    Size size = st.st_size;

    String result;
    result.count = size;
    result.data  = mem_alloc(mem, Char, .size=(result.count + 1 + extra_space));

    Size bytes_read = 0;
    while (bytes_read < size) {
        Auto r = read(fd, result.data + bytes_read, size - bytes_read);

        if (r == 0) {
            break;
        } else if (r < 0) {
            mem_free(mem, .old_ptr=result.data, .old_size=result.count);
            close(fd);
            return (String){};
        } else {
            bytes_read += r;
        }
    }

    result.data[bytes_read] = '\0';
    close(fd);
    return result;
}

Bool fs_write_entire_file (String path, String buf) {
    tmem_new(tm);

    Auto fd = open(cstr(tm, path), O_CREAT|O_WRONLY|O_TRUNC, 0744);
    if (fd < 0) return false;

    U64 bytes_written = 0;
    while (bytes_written < buf.count) {
        Auto r = write(fd, buf.data+bytes_written, buf.count-bytes_written);
        if (r == -1) return false;
        bytes_written += r;
    }

    close(fd);
    return true;
}

U64 fs_file_size (String path) {
    tmem_new(tm);
    struct stat st = {};
    Int r = stat(cstr(tm, path), &st);
    return (r == 0) ? st.st_size : 0;
}

Bool fs_copy (String oldpath, String newpath) {
    tmem_new(tm);

    Auto old_fd = open(cstr(tm, oldpath), O_RDONLY);
    if (old_fd < 0) return false;

    Auto new_fd = open(cstr(tm, newpath), O_WRONLY, 0744);
    if (new_fd < 0) { close(old_fd); return false; }

    U64 bytes_to_copy = fs_file_size(oldpath);
    U64 bytes_copied  = 0;
    Bool result = true;

    while (bytes_to_copy > 0) {
        off_t offset = bytes_copied;
        Int r = sendfile(new_fd, old_fd, &offset, bytes_to_copy);
        if (r < 0) { result = false; break; }
        if (r == 0) break;
        bytes_to_copy -= r;
        bytes_copied  += r;
    }

    close(old_fd);
    close(new_fd);
    return result;
}

String fs_get_current_working_dir (Mem *mem) {
    Auto buf = getcwd(0, 0);
    Auto result = astr_fmt(mem, "%s/", buf);
    free(buf);
    return result;
}

Bool fs_make_file_executable (String path) {
    tmem_new(tm);
    Auto result = chmod(cstr(tm, path), S_IRUSR | S_IWUSR | S_IXUSR);
    return result == 0;
}

Bool fs_file_exists (String path) {
    tmem_new(tm);
    Int r = access(cstr(tm, path), F_OK);
    return r == 0;
}

Bool fs_dir_exists (String path) {
    tmem_new(tm);
    struct stat st;
    Int r = stat(cstr(tm, path), &st);
    return (r == 0) ? S_ISDIR(st.st_mode) : false;
}

Bool fs_move (String oldpath, String newpath) {
    tmem_new(tm);
    Int r = rename(cstr(tm, oldpath), cstr(tm, newpath));
    return r == 0;
}

String fs_get_full_path (Mem *mem, String path) {
    Auto b = mem_alloc(mem, Char, .size=PATH_MAX);
    tmem_new(tm);
    Auto r = realpath(cstr(tm, path), b);
    return r ? str(b) : (String){};
}

Bool fs_delete_file (String path) {
    tmem_new(tm);
    Int r = remove(cstr(tm, path));
    return r == 0;
}

Bool fs_make_file (String path) {
    tmem_new(tm);
    Int fd = open(cstr(tm, path), O_CREAT | O_WRONLY | O_EXCL, 0644);
    if (fd < 0) return false;
    close(fd);
    return true;
}

Bool fs_make_dir (String path) {
    tmem_new(tm);
    Int r = mkdir(cstr(tm, path), 0755);
    return r == 0;
}

Void fs_ensure_dir (String path) {
    tmem_new(tm);
    ArrayString tokens;
    array_init(&tokens, tm);
    str_split(path, str("/"), false, false, &tokens);

    AString p = astr_new(tm);
    if (str_starts_with(path, str("/"))) astr_push_byte(&p, '/');

    array_iter (token, &tokens) {
        astr_push_str(&p, token);
        astr_push_byte(&p, '/');
        fs_make_dir(astr_to_str(&p));
    }
}

Void fs_ensure_file (String path, String init_path) {
    tmem_new(tm);
    ArrayString tokens;
    array_init(&tokens, tm);
    str_split(path, str("/"), false, false, &tokens);

    AString p = astr_new(tm);
    if (str_starts_with(path, str("/"))) astr_push_byte(&p, '/');

    array_iter (token, &tokens) {
        if (ARRAY_ITER_DONE) {
            astr_push_str(&p, token);
            String f = astr_to_str(&p);
            if (! fs_file_exists(f)) {
                fs_make_file(f);
                if (init_path.data) fs_copy(init_path, f);
            }
        } else {
            astr_push_str(&p, token);
            astr_push_byte(&p, '/');
            fs_make_dir(astr_to_str(&p));
        }
    }
}

istruct (FsIterLinux) {
    FsIter base;
    DIR *dir;
};

FsIter *fs_iter_new (Mem *mem, String path, Bool skip_dirs, Bool skip_files) {
    tmem_new(tm);
    if (path.count == 0) path = str("/");
    Auto it = mem_new(mem, FsIterLinux);
    it->base.skip_directories = skip_dirs;
    it->base.skip_files = skip_files;
    it->base.directory_path = path;
    it->base.mem = mem;
    it->base.current_full_path = astr_new(mem);
    it->dir = opendir(cstr(tm, path));
    return &it->base;
}

Bool fs_iter_next (FsIter *iter) {
    if (! cast(FsIterLinux*, iter)->dir) return false;

    while (true) {
        Auto entry = readdir(cast(FsIterLinux*, iter)->dir);
        if (! entry) return false;

        iter->current_full_path.count = 0;
        astr_push_fmt(&iter->current_full_path, "%.*s%s%s", STR(iter->directory_path), str_ends_with(iter->directory_path, str("/")) ? "" : "/", entry->d_name);
        iter->current_file_name = str(entry->d_name);

        struct stat st = {};
        Int r = stat(iter->current_full_path.data, &st);

        if (r == -1) continue;
        if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) continue;
        if (S_ISREG(st.st_mode) && iter->skip_files) continue;
        if (S_ISDIR(st.st_mode) && iter->skip_directories) continue;
        if (entry->d_name[0] == '.' && entry->d_name[1] == 0) continue;
        if (entry->d_name[0] == '.' && entry->d_name[1] == '.' && entry->d_name[2] == 0) continue;

        iter->is_directory = S_ISDIR(st.st_mode);
        break;
    }

    return true;
}

Void fs_iter_destroy (FsIter *iter) {
    if (! cast(FsIterLinux*, iter)->dir) return;
    closedir(cast(FsIterLinux*, iter)->dir);
    mem_free(iter->mem, .old_ptr=iter, .old_size=sizeof(FsIterLinux));
}
