#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <zlib.h>

char *type_id(uint8_t type) {
	switch(type) {
		case 1:
			return "commit";
		case 2:
			return "tree";
		case 3:
			return "blob";
		case 4:
			return "tag";
		case 5:
			return "rsvd";
		case 6:
			return "ofs_delt";
		case 7:
			return "ref_delt";
		default:
			return "other";
	}
}

int main(int argc, char **argv) {
	if(argc<1) { printf("Usage : %s packfile\n", argv[0]); exit(1); }
	FILE *f=fopen(argv[1],"r");
	if(!f) { perror("fopen source"); exit(1); }
	char magic[4];
	while(1) {
		fread(magic, 4, 1, f);
		if(strncmp(magic,"PACK",4)==0) break;
		else fseek(f, -3, SEEK_CUR);
	}
	uint32_t v_number;
	fread(&v_number, 4, 1, f);
	uint32_t n_obj;
	fread(&n_obj, 4, 1, f);
	n_obj=ntohl(n_obj);
	printf("n_obj=%d\n",n_obj);
	for(int i=0; i<n_obj; i++) {
		uint32_t length=0;
		uint8_t type;
		uint8_t x;
		fread(&x, 1, 1, f);
		type=(x & 0b01110000) >> 4;
		length=(x & 0b00001111);
		printf("type=%d\n", type);
		int j=0;
		while((x & 0b10000000)) {
			fread(&x, 1, 1, f);
			length += ((x&0b01111111) << (4+7*j));	
			j++;
		}
		printf(" length=%d\n", length);
#define ZBUFSZ 32768
		char s[256], sin[32768], sout[2*ZBUFSZ];
		snprintf(s, 256, "obj-%s-%d", type_id(type), i);
		FILE *ff=fopen(s,"w");
		if(!ff) { perror("fopen dest"); exit(1); }
		int nread=0, nreadd=0;
		struct z_stream_s zs;
		zs.next_in=(Bytef*)sin; zs.avail_in=0; zs.total_in=0;
		zs.next_out=(Bytef*)sout; zs.avail_out=2*ZBUFSZ; zs.total_out=0;
		zs.zalloc=Z_NULL; zs.zfree=Z_NULL; zs.opaque=Z_NULL;
		inflateInit(&zs);
		int zstat;
		long fbegin=ftell(f);
		printf("at byte %ld of input\n",fbegin);
		while(1) {
			printf("avail_in=%d\n", zs.avail_in);
			if(zs.avail_in==0) {
				nreadd=fread(sin, 1, ZBUFSZ, f);
				zs.avail_in=nreadd;
				if(nreadd<=0) { perror("fread source"); exit(1); }
			}
			zstat=inflate(&zs, Z_FINISH);
			if(zstat==Z_NEED_DICT || zstat==Z_DATA_ERROR || zstat==Z_STREAM_ERROR) {
				printf("deflate error %d %s\n", zstat, zs.msg);
				exit(1);
			}
			if(fwrite(sout, 2*ZBUFSZ-zs.avail_out, 1, ff)<=0) { perror("fwrite dest"); exit(1); }
			if(zstat == Z_STREAM_END)
				break;
			zs.next_out=(Bytef*)sout; zs.avail_out=2*ZBUFSZ;
		} 
		fclose(ff);
		inflateEnd(&zs);
		printf("read %lu compressed data\n", zs.total_in);
		fseek(f, fbegin+zs.total_in, SEEK_SET);
	//	break;
	}

}

