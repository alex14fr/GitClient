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
	printf("n_obj : %d\n", n_obj);
	for(uint32_t i=0; i<n_obj; i++) {
		printf("* obj_i=%d\n", i);
		uint32_t length=0;
		uint8_t type;
		uint8_t x;
		fread(&x, 1, 1, f);
		type=(x & 0b01110000) >> 4;
		length=(x & 0b00001111);
		int j=0;
		while((x & 0b10000000)) {
			fread(&x, 1, 1, f);
			length += ((x&0b01111111) << (4+7*j));	
			j++;
		}
		printf("type=%s length=%d\n", type_id(type), length);
		if(type<5) {
#define ZBUFSZ 32768
			char s[256], sin[ZBUFSZ], sout[2*ZBUFSZ];
			snprintf(s, 256, "obj-%d", i);
			FILE *ff=fopen(s,"w");
			if(!ff) { perror("fopen dest"); exit(1); }
			int nreadd=0;
			struct z_stream_s zs;
			zs.next_in=(Bytef*)sin; zs.avail_in=0; zs.total_in=0;
			zs.next_out=(Bytef*)sout; zs.avail_out=2*ZBUFSZ; zs.total_out=0;
			zs.zalloc=Z_NULL; zs.zfree=Z_NULL; zs.opaque=Z_NULL;
			inflateInit(&zs);
			int zstat;
			long fbegin=ftell(f);
			while(1) {
				if(zs.avail_in==0) {
					nreadd=fread(sin, 1, ZBUFSZ, f);
					zs.next_in=(Bytef*)sin;
					zs.avail_in=nreadd;
					if(nreadd<=0) { perror("fread source"); exit(1); }
				}
				zstat=inflate(&zs, Z_NO_FLUSH);
				if(zstat==Z_NEED_DICT || zstat==Z_DATA_ERROR || zstat==Z_STREAM_ERROR) {
					printf("deflate error %d %s\n", zstat, zs.msg);
					exit(1);
				}
				//printf("    zstat = %d   zs.total_in = %d    zs.total_out = %d    zs.avail_in = %d    zs.avail_out = %d\n", zstat, zs.total_in, zs.total_out, zs.avail_in, zs.avail_out);
				if(2*ZBUFSZ > zs.avail_out) {
					if(fwrite(sout, 2*ZBUFSZ-zs.avail_out, 1, ff)<=0) { perror("fwrite dest"); exit(1); }
				}
				if(zstat == Z_STREAM_END)
					break;
				zs.next_out=(Bytef*)sout; zs.avail_out=2*ZBUFSZ;
			} 
			fclose(ff);
			inflateEnd(&zs);
			fseek(f, fbegin+zs.total_in, SEEK_SET);
			if(length!=zs.total_out) 
				printf("warning: deflated %ld bytes, expected %u bytes\n", zs.total_out, length); 
		} else {
			if(type != 7) { printf("todo\n"); exit(1); }
			if(type == 7) {
				char baseobj[20];
				printf("baseobj : ");
				for(int k=0;k<20;k++) {
					fread(baseobj+k, 1, 1, f);
					printf("%hhx",baseobj[k]);
				}
				printf("\n");
			}
			char sin[ZBUFSZ], *sout;
			sout=malloc(length);
			long pos=ftell(f);
			struct z_stream_s zs;
			zs.next_in=(Bytef*)sin; zs.avail_in=0; zs.total_in=0;
			zs.next_out=(Bytef*)sout; zs.avail_out=length; zs.total_out=0;
			zs.zalloc=Z_NULL; zs.zfree=Z_NULL; zs.opaque=Z_NULL;
			inflateInit(&zs);
			while(1) {
				if(zs.avail_in==0) {
					int nreadd=fread(sin, 1, ZBUFSZ, f);
					zs.next_in=(Bytef*)sin;
					zs.avail_in=nreadd;
				}
				int zstat=inflate(&zs, Z_NO_FLUSH);
				if(zstat==Z_NEED_DICT || zstat==Z_DATA_ERROR || zstat==Z_STREAM_ERROR) {
					printf("deflate error %d %s\n", zstat, zs.msg);
					exit(1);
				}
				printf("    zstat = %d   zs.total_in = %d    zs.total_out = %d    zs.avail_in = %d    zs.avail_out = %d\n", zstat, zs.total_in, zs.total_out, zs.avail_in, zs.avail_out);
				if(zstat==Z_STREAM_END)
					break;
			};
			fseek(f, pos+zs.total_in, SEEK_SET);
			inflateEnd(&zs);
			/*
			printf("delta data:\n");
			for(uint32_t k=0;k<length;k++) printf("%hhx ", sout[k]);
			printf("\n");
			*/
			uint32_t sz_baseobj=0, sz_targobj=0;
			int j=0;
			do {
				x=*sout; sout++; 
				sz_baseobj += ((x & 0b01111111) << (7*j));
				j++;
			} while((x & 0b10000000)!=0);
			j=0;
			do {
				x=*sout; sout++; 
				sz_targobj += ((x & 0b01111111) << (7*j));
				j++;
			} while((x & 0b10000000)!=0);
			uint32_t targobj_done=0;
			while(targobj_done < sz_targobj) {
				x=*sout; sout++; 
				if((x&0b10000000)==0) {
					printf("append data length=%d: \n",x&0b01111111);
					char tmp;
					for(int k=0;k<(x&0b01111111);k++) {
						tmp=*sout; sout++;
						printf("%hhx ",tmp);
					}
					printf("\n");
					targobj_done += (x&0b01111111);
				} else {
					uint32_t offset=0;
					uint32_t size=0;
					uint8_t tmp;
					if((x&1)!=0) { tmp=*sout; sout++; offset+=tmp; }
					if((x&2)!=0) { tmp=*sout; sout++; offset+=(tmp << 8); }
					if((x&4)!=0) { tmp=*sout; sout++; offset+=(tmp << 16); }
					if((x&8)!=0) { tmp=*sout; sout++; offset+=(tmp << 24); }
					if((x&16)!=0) { tmp=*sout; sout++; size+=tmp; }
					if((x&32)!=0) { tmp=*sout; sout++; size+=(tmp << 8); }
					if((x&64)!=0) { tmp=*sout; sout++; size+=(tmp << 16); }
					if(size==0) size=0x10000;
					printf("copy from base offset=%d size=%d\n", offset, size);
					targobj_done += size;
				}
				//printf("targobj_done=%d sz_targobj=%d\n",targobj_done,sz_targobj);
			}
			if(targobj_done != sz_targobj) { printf("warning: targobj_done=%d, expected %d\n", targobj_done, sz_targobj); }
			free(sout-length);
		}
	//	break;
	}

}

