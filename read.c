#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

typedef struct {
	uint32_t sig __attribute__ ((packed));
	uint16_t version __attribute__ ((packed));
	uint16_t gen __attribute__ ((packed));
	uint16_t compression __attribute__ ((packed));
	uint16_t modtime __attribute__ ((packed));
	uint16_t moddate __attribute__ ((packed));
	uint32_t crc32 __attribute__ ((packed));
	uint32_t compressedsize __attribute__ ((packed));
	uint32_t uncompressedsize __attribute__ ((packed));
	uint16_t filenamelength __attribute__ ((packed));
	uint16_t extrafieldlength __attribute__ ((packed));
} lfh;
typedef struct {
	uint32_t crc32 __attribute__ ((packed));
	uint32_t compressedsize __attribute__ ((packed));
	uint32_t uncompressedsize __attribute__ ((packed));
} dd;

int main(int argc, char *argv[])
{
	if (argc != 2)
		return -1;
	FILE *fh = fopen(argv[1], "rb");
	FILE *ofh;
	lfh header;
	dd des;
	uint32_t sig;
	int first = 1;
	unsigned long lastloc;
	unsigned long startloc;
	char filename[UINT16_MAX];
	char buffer[0x10000];
	char zbuffer[0x10000];
	int i, len, ret;
	z_stream strm;
	while (!feof(fh)) {
		if (!first) {
			fread(&sig, sizeof(sig), 1, fh);
			if (sig == 0x04034b50) {
				printf("All is going according to plan.\n");
				fseek(fh, -sizeof(sig), SEEK_CUR);
			} else {
				printf("Brute-force seeking til next sig.\n");
				fseek(fh, lastloc + sizeof(header.sig), SEEK_SET);
				while (!feof(fh)) {
					fread(&sig, sizeof(sig), 1, fh);
					if (sig == 0x04034b50) {
						fseek(fh, -sizeof(sig), SEEK_CUR);
						break;
					} else
						fseek(fh, -(sizeof(sig) - 1), SEEK_CUR);
				}
			}
		}
		lastloc = ftell(fh);
		fread(&header, sizeof(header), 1, fh);
		fread(filename, sizeof(char), header.filenamelength, fh);
		filename[header.filenamelength] = 0;
		fseek(fh, header.extrafieldlength, SEEK_CUR);
		startloc = ftell(fh);
		if (header.gen & (1 << 3)) {
			while (!feof(fh)) {
				fread(&sig, sizeof(sig), 1, fh);
				if (sig == 0x08074b50)
					break;
				else
					fseek(fh, -(sizeof(sig) - 1), SEEK_CUR);
			}
			fread(&des, sizeof(des), 1, fh);
			header.crc32 = des.crc32;
			header.compressedsize = des.compressedsize;
			header.uncompressedsize = des.uncompressedsize;
		}
		fseek(fh, startloc, SEEK_SET);
		if (!header.compressedsize) {
			printf("Making directory %s\n", filename);
			for (i = 0; i < strlen(filename); ++i) {
				if (filename[i] == '/') {
					filename[i] = 0;
					mkdir(filename, S_IRWXU);
					filename[i] = '/';
				}
			}
		} else {
			printf("Writing %s of length %f MB\n", filename, (float)header.compressedsize / (1024.0f * 1024.0f));
			ofh = fopen(filename, "wb");
			if (header.compression == 8) {
				strm.zalloc = Z_NULL;
				strm.zfree = Z_NULL;
				strm.opaque = Z_NULL;
				strm.avail_in = 0;
				strm.next_in = Z_NULL;
				inflateInit2(&strm, -MAX_WBITS);
				printf("Deflating.\n");
			}
			while ((i = ftell(fh)) < (startloc + header.compressedsize)) {
				len = startloc + header.compressedsize - i;
				if (len > sizeof(buffer))
					len = sizeof(buffer);
				len = fread(buffer, 1, len, fh);
				if (header.compression == 8) {
					strm.avail_in = len;
					strm.next_in = buffer;
					do {
						strm.avail_out = sizeof(zbuffer);
						strm.next_out = zbuffer;
						if ((ret = inflate(&strm, Z_NO_FLUSH)) != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
							printf("Inflation failed, error %i.\n", ret);
							fseek(fh, startloc + header.compressedsize, SEEK_SET);
							break;
						}
						len = sizeof(zbuffer) - strm.avail_out;
						fwrite(zbuffer, 1, len, ofh);
					} while (strm.avail_out == 0);
				} else
					fwrite(buffer, 1, len, ofh);
			}
			if (header.compression == 8)
				inflateEnd(&strm);
			fclose(ofh);
		}
		if (header.gen & (1 << 3))
			fseek(fh, sizeof(des) + sizeof(uint32_t), SEEK_CUR);
		first = 0;
	}
	fclose(fh);
	return 0;
}
