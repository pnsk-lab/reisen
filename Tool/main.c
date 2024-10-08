/* $Id$ */

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <zlib.h>

#include "../version.h"

extern unsigned char sfx[];
extern unsigned int sfx_len;
uint32_t total;
const char* projname;

#define COMPRESS 65535

int scan(FILE* f, const char* base, const char* pref);

FILE* prepare_file(const char* path){
	FILE* f = fopen(path, "wb");
	if(f != NULL){
		fwrite(sfx, 1, sfx_len, f);
	}
	return f;
}

void read_config(FILE* f, char* config){
	int i;
	int incr = 0;
	for(i = 0;; i++){
		if(config[i] == '\n' || config[i] == 0){
			char oldc = config[i];
			char* line = config + incr;
			config[i] = 0;

			if(strlen(line) > 0 && line[0] != '#'){
				int j;
				for(j = 0;; j++){
					if(line[j] == ' ' || line[j] == '\t' || line[j] == 0){
						bool hasarg = line[j] != 0;
						char* arg = NULL;
						line[j] = 0;
						if(hasarg){
							j++;
							for(; line[j] != 0 && (line[j] == '\t' || line[j] == ' '); j++);
							arg = line + j;
						}
						if(strcmp(line, "Include") == 0 && hasarg){
							scan(f, arg, "");
						}
						break;
					}
				}
			}

			config[i] = oldc;
			incr = i + 1;
			if(oldc == 0) break;
		}
	}
}

int scan(FILE* f, const char* base, const char* pref){
	DIR* dir = opendir(base);
	if(dir != NULL){
		struct dirent* d;
		while((d = readdir(dir)) != NULL){
			if(strcmp(d->d_name, "..") != 0 && strcmp(d->d_name, ".") != 0){
				struct stat s;
				char* path = malloc(1 + strlen(base) + 1 + strlen(d->d_name));
				strcpy(path, base);
				path[strlen(base)] = '/';
				strcpy(path + strlen(base) + 1, d->d_name);
				path[strlen(base) + 1 + strlen(d->d_name)] = 0;
				if(stat(path, &s) != 0){
					free(path);
					closedir(dir);
					return 1;
				}else{
					if(S_ISDIR(s.st_mode)){
						char* pt;
						int i;
						unsigned char byt;
						uint32_t written = 0;
						printf("Adding directory %s\n", path);
						pt = malloc(1 + strlen(pref) + 1 + strlen(d->d_name));
						strcpy(pt, pref);
						pt[strlen(pref)] = '/';
						strcpy(pt + strlen(pref) + 1, d->d_name);
						pt[strlen(pref) + 1 + strlen(d->d_name)] = 0;

						fwrite(pt, 1, strlen(pt), f);
						written = strlen(pt);

						total += 5 + written;

						for(i = 0; i < 4; i++){
							byt = (written & 0xff000000) >> 24;
							written = written << 8;
							fwrite(&byt, 1, 1, f);
						}
						byt = 0;
						fwrite(&byt, 1, 1, f);
						if(scan(f, path, pt) != 0){
							int i;
							free(pt);
							free(path);
							closedir(dir);
							return 1;
						}
						free(pt);
					}else{
						z_stream strm;
						struct stat s;
						FILE* src = fopen(path, "rb");
						unsigned char in[COMPRESS];
						unsigned char out[COMPRESS];
						int flush;
						uint8_t byt;
						uint32_t written = 0;
						int i;
						char* pt;

						stat(path, &s);

						pt = malloc(1 + strlen(pref) + 1 + strlen(d->d_name));
						strcpy(pt, pref);
						pt[strlen(pref)] = '/';
						strcpy(pt + strlen(pref) + 1, d->d_name);
						pt[strlen(pref) + 1 + strlen(d->d_name)] = 0;

						strm.zalloc = Z_NULL;
						strm.zfree = Z_NULL;
						strm.opaque = Z_NULL;
						if(deflateInit(&strm, Z_BEST_COMPRESSION) != Z_OK || src == NULL){
							free(path);
							free(pt);
							closedir(dir);
							return 1;
						}

						printf("Compressing %s... ", path);
						fflush(stdout);

						fwrite(pt, 1, strlen(pt), f);
						byt = strlen(pt);
						fwrite(&byt, 1, 1, f);

						do{
							strm.avail_in = fread(in, 1, COMPRESS, src);
							flush = feof(src) ? Z_FINISH : Z_NO_FLUSH;
							strm.next_in = in;
							do{
								int have;
								strm.avail_out = COMPRESS;
								strm.next_out = out;
								deflate(&strm, flush);
								have = COMPRESS - strm.avail_out;
								written += have;
								fwrite(out, 1, have, f);
							}while(strm.avail_out == 0);
						}while(flush != Z_FINISH);

						printf("done, %lu bytes, %d%%\n", (unsigned long)written, (int)(((double)written / s.st_size) * 100));

						total += strlen(pt) + 1 + written + 4 + 1;

						for(i = 0; i < 4; i++){
							byt = (written & 0xff000000) >> 24;
							written = written << 8;
							fwrite(&byt, 1, 1, f);
						}
						byt = 1;
						fwrite(&byt, 1, 1, f);

						deflateEnd(&strm);
						fclose(src);
						free(pt);
					}
				}
				free(path);
			}
		}
		closedir(dir);
	}
}

int main(int argc, char** argv){
	int i;
	const char* input = NULL;
	const char* output = NULL;
	projname = NULL;
	for(i = 1; i < argc; i++){
		if(argv[i][0] == '-'){
			if(strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0){
				printf("Reisen version %s using zlib version %s\n", REISEN_VERSION, zlibVersion());
				return 0;
			}else{
				fprintf(stderr, "Invalid option: %s\n", argv[i]);
				return 1;
			}
		}else{
			if(projname == NULL){
				projname = argv[i];
			}else if(input == NULL){
				input = argv[i];
			}else if(output == NULL){
				output = argv[i];
			}else{
				fprintf(stderr, "Too many arguments\n");
			}
		}
	}
	if(projname == NULL || input == NULL || output == NULL){
		fprintf(stderr, "Usage: %s [options] project_name input output\n", argv[0]);
		return 1;
	}else{
		FILE* f;
		char name[256];
		memset(name, 0, 256);
		printf("Creating output: %s\n", output);
		f = prepare_file(output);
		if(f == NULL){
			fprintf(stderr, "Failed to create one\n");
			return 1;
		}
		if(input[0] == '@'){
			struct stat s;
			if(stat(input + 1, &s) == 0){
				uint8_t byt;
				uint32_t written = s.st_size;
				char* buf;
				FILE* fin = fopen(input + 1, "r");
				buf = malloc(s.st_size + 1);
				buf[s.st_size] = 0;
				fread(buf, 1, s.st_size, fin);
				fwrite(buf, 1, s.st_size, f);

				fclose(fin);
				for(i = 0; i < 4; i++){
					byt = (written & 0xff000000) >> 24;
					written = written << 8;
					fwrite(&byt, 1, 1, f);
				}
				byt = 2;
				fwrite(&byt, 1, 1, f);
				total += s.st_size + 4 + 1;

				read_config(f, buf);
			}else{
				fprintf(stderr, "Failed to process\n");
				remove(output);
				return 1;
			}
		}else{
			if(scan(f, input, "") != 0){
				fprintf(stderr, "Failed to process\n");
				fclose(f);
				remove(output);
				return 1;
			}
		}
		printf("Total: %lu bytes\n", (unsigned long)total);
		strcpy(name, projname);
		fwrite(name, 1, 256, f);
		for(i = 0; i < 4; i++){
			unsigned char byt = (total & 0xff000000) >> 24;
			total = total << 8;
			fwrite(&byt, 1, 1, f);
		}
		fclose(f);
	}
}
