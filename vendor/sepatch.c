// usage: sepatch <in policy> <out policy> < rules
// rules: one "allow <source> <target> <class> <perm>..." per line, '#' starts a comment
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sepol/policydb/policydb.h>
#include <sepol/policydb/avtab.h>
#include <sepol/policydb/hashtab.h>

static policydb_t pdb;

static uint16_t type_value(const char *name)
{
	type_datum_t *t = hashtab_search(pdb.p_types.table, (hashtab_key_t)name);
	if (!t) {
		fprintf(stderr, "unknown type: %s\n", name);
		exit(1);
	}
	return t->s.value;
}

static void allow(const char *s, const char *t, const char *c, char *perms)
{
	class_datum_t *cls = hashtab_search(pdb.p_classes.table, (hashtab_key_t)c);
	if (!cls) {
		fprintf(stderr, "unknown class: %s\n", c);
		exit(1);
	}
	uint32_t av = 0;
	for (char *p = strtok(perms, " \t"); p; p = strtok(NULL, " \t")) {
		perm_datum_t *pd = hashtab_search(cls->permissions.table, p);
		if (!pd && cls->comdatum)
			pd = hashtab_search(cls->comdatum->permissions.table, p);
		if (!pd) {
			fprintf(stderr, "unknown permission: %s %s\n", c, p);
			exit(1);
		}
		av |= 1U << (pd->s.value - 1);
	}
	avtab_key_t key = {
		.source_type = type_value(s),
		.target_type = type_value(t),
		.target_class = cls->s.value,
		.specified = AVTAB_ALLOWED,
	};
	avtab_datum_t *d = avtab_search(&pdb.te_avtab, &key);
	if (d) {
		d->data |= av;
	} else {
		avtab_datum_t nd = { .data = av };
		if (avtab_insert(&pdb.te_avtab, &key, &nd)) {
			fprintf(stderr, "avtab_insert failed\n");
			exit(1);
		}
	}
}

static uint32_t header_config(const char *path)
{
	unsigned char h[24];
	FILE *f = fopen(path, "rb");
	if (!f || fread(h, 1, sizeof(h), f) != sizeof(h) || memcmp(h + 8, "SE Linux", 8)) {
		fprintf(stderr, "%s: not a kernel policy\n", path);
		exit(1);
	}
	fclose(f);
	return h[20] | h[21] << 8 | h[22] << 16 | (uint32_t)h[23] << 24;
}

int main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: %s <in policy> <out policy> < rules\n", argv[0]);
		return 2;
	}

	struct policy_file pf;
	policy_file_init(&pf);
	pf.type = PF_USE_STDIO;
	pf.fp = fopen(argv[1], "rb");
	if (!pf.fp || policydb_init(&pdb) || policydb_read(&pdb, &pf, 0)) {
		fprintf(stderr, "%s: read failed\n", argv[1]);
		return 1;
	}
	fclose(pf.fp);

	char line[4096];
	while (fgets(line, sizeof(line), stdin)) {
		line[strcspn(line, "#\n")] = 0;
		char *save, *verb = strtok_r(line, " \t", &save);
		if (!verb)
			continue;
		char *s = strtok_r(NULL, " \t", &save);
		char *t = strtok_r(NULL, " \t", &save);
		char *c = strtok_r(NULL, " \t", &save);
		if (strcmp(verb, "allow") || !s || !t || !c || !save || !*save) {
			fprintf(stderr, "bad rule: %s\n", line);
			return 1;
		}
		allow(s, t, c, save);
	}

	pf.fp = fopen(argv[2], "wb");
	if (!pf.fp || policydb_write(&pdb, &pf) || fclose(pf.fp)) {
		fprintf(stderr, "%s: write failed\n", argv[2]);
		return 1;
	}

	// libsepol drops the Android config bits (netlink route/getneigh), put the original word back
	uint32_t cfg = header_config(argv[1]);
	header_config(argv[2]);
	FILE *f = fopen(argv[2], "r+b");
	unsigned char b[4] = { cfg, cfg >> 8, cfg >> 16, cfg >> 24 };
	if (!f || fseek(f, 20, SEEK_SET) || fwrite(b, 1, 4, f) != 4 || fclose(f)) {
		fprintf(stderr, "%s: header write failed\n", argv[2]);
		return 1;
	}
	return 0;
}
