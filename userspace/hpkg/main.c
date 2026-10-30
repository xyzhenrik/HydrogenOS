#include "repo.h"

#include "mini_libc.h"

/*
 * hpkg - package manager. See docs/HPKG.md.
 *
 *   hpkg [--root DIR] [--repo DIR] <command> [arguments]
 *
 * Commands: install, remove, list, info, verify, update.
 */

#define DEFAULT_REPOSITORY "/var/lib/hpkg/repo"

enum {
    STATUS_FAILURE = 1,
    STATUS_USAGE = 2,
    REQUIRED_BY_MAX = 32,
};

typedef struct Options {
    HpkgStore store;
    char repository[HPKG_ROOT_MAX + sizeof(DEFAULT_REPOSITORY)];
    int repository_given;
} Options;

static void print_usage(void) {
    dprintf(2,
            "usage: hpkg [--root DIR] [--repo DIR] <command> [arguments]\n"
            "  install <file.hpkg | name>...  install packages from files or the repository\n"
            "  remove <name>...               remove installed packages\n"
            "  list                           list installed packages\n"
            "  info <name>                    show details of a package\n"
            "  verify [name...]               check installed files against the database\n"
            "  update                         check the repository and show available upgrades\n");
}

static int is_file_argument(const char *argument) {
    size_t length = strlen(argument);

    return strchr(argument, '/') != 0 ||
           (length > 5 && strcmp(argument + length - 5, ".hpkg") == 0);
}

static int install_archive(const Options *options, HpkgArchive *archive) {
    HpkgPackageSet installed;
    const HpkgPackage *previous;
    char old_version[HPKG_VERSION_MAX + 1] = "";

    if (hpkg_store_load_all(&options->store, &installed) != 0)
        return HPKG_FAILED;
    previous = hpkg_package_set_find(&installed, archive->meta.name);
    if (previous) {
        int order = hpkg_version_compare(archive->meta.version, previous->meta.version);

        if (order == 0) {
            printf("%s %s is already installed\n", archive->meta.name, archive->meta.version);
            hpkg_package_set_free(&installed);
            return 0;
        }
        if (order < 0) {
            hpkg_report("%s %s is installed; refusing to downgrade to %s", archive->meta.name,
                        previous->meta.version, archive->meta.version);
            hpkg_package_set_free(&installed);
            return HPKG_FAILED;
        }
        strcpy(old_version, previous->meta.version);
    }
    hpkg_package_set_free(&installed);
    if (hpkg_store_install(&options->store, archive) != 0)
        return HPKG_FAILED;
    if (old_version[0])
        printf("upgraded %s %s -> %s\n", archive->meta.name, old_version, archive->meta.version);
    else
        printf("installed %s %s (%u file%s)\n", archive->meta.name, archive->meta.version,
               (unsigned)archive->entry_count, archive->entry_count == 1 ? "" : "s");
    return 0;
}

static int install_file(const Options *options, const char *path) {
    HpkgArchive archive;
    int status;

    if (hpkg_archive_open(path, &archive) != 0)
        return HPKG_FAILED;
    status = install_archive(options, &archive);
    hpkg_archive_close(&archive);
    return status;
}

/* Removes the dependencies a failed repository installation had added. */
static void undo_installed(const Options *options, const HpkgIndexEntry **order, size_t count) {
    while (count-- > 0) {
        if (hpkg_store_remove(&options->store, order[count]->meta.name) != 0)
            hpkg_report("could not undo the installation of %s", order[count]->meta.name);
    }
}

static int install_from_repository(const Options *options, const char *name) {
    HpkgIndex index;
    HpkgPackageSet installed;
    const HpkgIndexEntry **order = 0;
    size_t count = 0;
    int status = HPKG_FAILED;

    if (hpkg_index_load(options->repository, &index) != 0)
        return HPKG_FAILED;
    if (hpkg_store_load_all(&options->store, &installed) != 0) {
        hpkg_index_free(&index);
        return HPKG_FAILED;
    }
    if (hpkg_repo_plan(&index, &installed, name, &order, &count) != 0)
        goto done;
    for (size_t i = 0; i < count; ++i) {
        HpkgArchive archive = {0};

        if (hpkg_repo_open_package(options->repository, order[i], &archive) != 0 ||
            install_archive(options, &archive) != 0) {
            if (archive.buffer)
                hpkg_archive_close(&archive);
            undo_installed(options, order, i);
            goto done;
        }
        hpkg_archive_close(&archive);
    }
    status = 0;
done:
    free(order);
    hpkg_package_set_free(&installed);
    hpkg_index_free(&index);
    return status;
}

static int command_install(const Options *options, int count, char **names) {
    int status = 0;

    if (count < 1) {
        print_usage();
        return STATUS_USAGE;
    }
    for (int i = 0; i < count; ++i) {
        int result = is_file_argument(names[i]) ? install_file(options, names[i])
                                                : install_from_repository(options, names[i]);

        if (result != 0)
            status = STATUS_FAILURE;
    }
    return status;
}

static int command_remove(const Options *options, int count, char **names) {
    int status = 0;

    if (count < 1) {
        print_usage();
        return STATUS_USAGE;
    }
    for (int i = 0; i < count; ++i) {
        int result = hpkg_store_remove(&options->store, names[i]);

        if (result == 0) {
            printf("removed %s\n", names[i]);
        } else {
            if (result == HPKG_NOT_FOUND)
                hpkg_report("%s is not installed", names[i]);
            status = STATUS_FAILURE;
        }
    }
    return status;
}

static int command_list(const Options *options) {
    HpkgPackageSet installed;

    if (hpkg_store_load_all(&options->store, &installed) != 0)
        return STATUS_FAILURE;
    for (size_t i = 0; i < installed.count; ++i)
        printf("%s %s\n", installed.packages[i].meta.name, installed.packages[i].meta.version);
    hpkg_package_set_free(&installed);
    return 0;
}

static void print_names(const char *label, const char names[][HPKG_NAME_MAX + 1],
                        size_t count) {
    printf("%-15s:", label);
    for (size_t i = 0; i < count; ++i)
        printf(" %s", names[i]);
    if (count == 0)
        printf(" none");
    printf("\n");
}

static void print_installed_info(const HpkgPackageSet *installed, const HpkgPackage *package) {
    char required_by[REQUIRED_BY_MAX][HPKG_NAME_MAX + 1];
    size_t required_count = 0;
    uint64_t total = 0;

    for (size_t i = 0; i < installed->count; ++i) {
        if (hpkg_meta_depends_on(&installed->packages[i].meta, package->meta.name) &&
            required_count < REQUIRED_BY_MAX)
            strcpy(required_by[required_count++], installed->packages[i].meta.name);
    }
    for (size_t i = 0; i < package->file_count; ++i)
        total += package->files[i].size;
    printf("%-15s: %s\n", "Name", package->meta.name);
    printf("%-15s: %s\n", "Version", package->meta.version);
    printf("%-15s: %s\n", "Description", package->meta.description[0] ? package->meta.description : "none");
    print_names("Depends on", package->meta.depends, package->meta.depend_count);
    print_names("Required by", required_by, required_count);
    printf("%-15s: %lu bytes in %lu file%s\n", "Installed size", (unsigned long)total,
           (unsigned long)package->file_count, package->file_count == 1 ? "" : "s");
    printf("Files:\n");
    for (size_t i = 0; i < package->file_count; ++i)
        printf("  %s\n", package->files[i].path);
}

static int command_info(const Options *options, int count, char **names) {
    HpkgPackageSet installed;
    const HpkgPackage *package;
    int status = 0;

    if (count != 1) {
        print_usage();
        return STATUS_USAGE;
    }
    if (hpkg_store_load_all(&options->store, &installed) != 0)
        return STATUS_FAILURE;
    package = hpkg_package_set_find(&installed, names[0]);
    if (package) {
        print_installed_info(&installed, package);
    } else {
        HpkgIndex index;
        const HpkgIndexEntry *entry = 0;

        if (hpkg_index_load(options->repository, &index) == 0) {
            entry = hpkg_index_find(&index, names[0]);
            if (entry) {
                printf("%-15s: %s\n", "Name", entry->meta.name);
                printf("%-15s: %s\n", "Version", entry->meta.version);
                printf("%-15s: %s\n", "Description",
                       entry->meta.description[0] ? entry->meta.description : "none");
                print_names("Depends on", entry->meta.depends, entry->meta.depend_count);
                printf("%-15s: %lu bytes\n", "Download size", (unsigned long)entry->size);
                printf("Not installed; available in the repository.\n");
            }
            hpkg_index_free(&index);
        }
        if (!entry) {
            hpkg_report("%s is neither installed nor in the repository", names[0]);
            status = STATUS_FAILURE;
        }
    }
    hpkg_package_set_free(&installed);
    return status;
}

static int command_verify(const Options *options, int count, char **names) {
    HpkgPackageSet installed;
    int problems = 0;
    int checked = 0;
    int status = 0;

    if (hpkg_store_load_all(&options->store, &installed) != 0)
        return STATUS_FAILURE;
    for (int i = 0; i < (count ? count : (int)installed.count); ++i) {
        const HpkgPackage *package = count ? hpkg_package_set_find(&installed, names[i])
                                           : &installed.packages[i];
        int found;

        if (!package) {
            hpkg_report("%s is not installed", names[i]);
            status = STATUS_FAILURE;
            continue;
        }
        found = hpkg_store_verify(&options->store, package);
        if (found < 0) {
            status = STATUS_FAILURE;
            continue;
        }
        problems += found;
        ++checked;
    }
    hpkg_package_set_free(&installed);
    printf("verified %d package%s, %d problem%s\n", checked, checked == 1 ? "" : "s", problems,
           problems == 1 ? "" : "s");
    return status != 0 || problems > 0 ? STATUS_FAILURE : 0;
}

static int command_update(const Options *options) {
    HpkgIndex index;
    HpkgPackageSet installed;
    size_t upgradable = 0;
    int status = 0;

    if (hpkg_index_load(options->repository, &index) != 0)
        return STATUS_FAILURE;
    if (hpkg_store_load_all(&options->store, &installed) != 0) {
        hpkg_index_free(&index);
        return STATUS_FAILURE;
    }
    for (size_t i = 0; i < index.count; ++i) {
        const HpkgIndexEntry *entry = &index.entries[i];
        const HpkgPackage *package;
        int check = hpkg_repo_check_package(options->repository, entry);

        if (check == HPKG_NOT_FOUND)
            hpkg_report("%s: %s is missing from the repository", entry->meta.name, entry->file);
        if (check != 0) {
            status = STATUS_FAILURE;
            continue;
        }
        package = hpkg_package_set_find(&installed, entry->meta.name);
        if (package && hpkg_version_compare(entry->meta.version, package->meta.version) > 0) {
            printf("%s %s -> %s\n", entry->meta.name, package->meta.version, entry->meta.version);
            ++upgradable;
        }
    }
    printf("repository: %lu packages, %lu upgradable\n", (unsigned long)index.count,
           (unsigned long)upgradable);
    hpkg_package_set_free(&installed);
    hpkg_index_free(&index);
    return status;
}

static int parse_options(Options *options, int *argc, char ***argv) {
    const char *root = "";
    const char *repository = 0;
    int index = 1;

    while (index < *argc && strncmp((*argv)[index], "--", 2) == 0) {
        const char *option = (*argv)[index];

        if (index + 1 >= *argc)
            return -1;
        if (strcmp(option, "--root") == 0)
            root = (*argv)[index + 1];
        else if (strcmp(option, "--repo") == 0)
            repository = (*argv)[index + 1];
        else
            return -1;
        index += 2;
    }
    if (hpkg_store_open(&options->store, root) != 0)
        return STATUS_FAILURE;
    if (repository) {
        if (strlen(repository) >= sizeof(options->repository)) {
            hpkg_report("repository path too long");
            return STATUS_FAILURE;
        }
        strcpy(options->repository, repository);
    } else if (hpkg_store_path(&options->store, DEFAULT_REPOSITORY, options->repository,
                               sizeof(options->repository)) != 0) {
        return STATUS_FAILURE;
    }
    *argc -= index;
    *argv += index;
    return 0;
}

int main(int argc, char **argv) {
    Options options;
    const char *command;
    int status = parse_options(&options, &argc, &argv);

    if (status > 0)
        return status;
    if (status < 0 || argc < 1) {
        print_usage();
        return STATUS_USAGE;
    }
    command = argv[0];
    if (strcmp(command, "install") == 0)
        return command_install(&options, argc - 1, argv + 1);
    if (strcmp(command, "remove") == 0)
        return command_remove(&options, argc - 1, argv + 1);
    if (strcmp(command, "list") == 0 && argc == 1)
        return command_list(&options);
    if (strcmp(command, "info") == 0)
        return command_info(&options, argc - 1, argv + 1);
    if (strcmp(command, "verify") == 0)
        return command_verify(&options, argc - 1, argv + 1);
    if (strcmp(command, "update") == 0 && argc == 1)
        return command_update(&options);
    print_usage();
    return STATUS_USAGE;
}
