/*
 * openwatcom2irc — OMF → ELF64 post-processor
 * Reads the OMF .obj written by the standard cg, extracts code bytes
 * and symbol info, writes a valid ELF64 relocatable object.
 * Based on the proven elf64_emit.c prototype from Phase 16.
 */
#include "_cgstd.h"
#include "coderep.h"
#include "pcencode.h"
#include "feprotos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <elf.h>

static char *obj_filename;
static bool  x64_active;

/* OMF record types */
#define OMF_LEDATA32  0xA1
#define OMF_LEDATA    0xA0
#define OMF_EXTDEF    0x8C
#define OMF_PUBDEF32  0x91
#define OMF_PUBDEF    0x90

void X64ObjInit( void )
{
    const char *name = FEAuxInfo( NULL, FEINF_OBJECT_FILE_NAME );
    obj_filename = strdup( name ? name : "output.o" );
    x64_active = true;
    /* Tell the dispatch that x64 post-processing is active.
     * This is checked by X64CheckDispatch/X64IsActive. */
    { extern void X64SetActive(bool); X64SetActive(true); }
}

void X64ObjFini( void )
{
    FILE *fp_in, *fp_out;
    unsigned char *omf;
    long omf_size;
    unsigned char *code;
    int code_len = 0;
    int i, rec_len;
    char *temp_name;
    
    /* Collected external names */
    char *ext_names[64];
    int ext_count = 0;
    
    if( !x64_active || !obj_filename ) return;
    
    /* Save OMF copy for debug */
    {
        char dbg_name[256];
        snprintf(dbg_name, 256, "%s.omf", obj_filename);
        FILE *dbg = fopen(dbg_name, "wb");
        if(dbg) { 
            FILE *src = fopen(obj_filename, "rb");
            if(src) {
                char buf[4096]; int n;
                while((n=fread(buf,1,4096,src))>0) fwrite(buf,1,n,dbg);
                fclose(src);
            }
            fclose(dbg);
        }
    }
    
    /* Read OMF file */
    fp_in = fopen( obj_filename, "rb" );
    if( !fp_in ) return;
    fseek( fp_in, 0, SEEK_END );
    omf_size = ftell( fp_in );
    fseek( fp_in, 0, SEEK_SET );
    omf = (unsigned char *)malloc( omf_size );
    fread( omf, 1, omf_size, fp_in );
    fclose( fp_in );
    
    code = (unsigned char *)malloc( omf_size );
    
    /* Debug: dump all OMF records */
    {
        int di = 0;
        while(di < omf_size && di+3 <= omf_size) {
            unsigned char drt = omf[di];
            int drl = omf[di+1] | (omf[di+2] << 8);
            fprintf(stderr, "OMF[%04x] type=%02x len=%d", di, drt, drl);
            if(drt==0xA1||drt==0xA0) {
                fprintf(stderr, " LEDATA");
                int k=di+3; 
                for(int m=0; m<8 && m<drl; m++)
                    fprintf(stderr, " %02x", omf[k+m]);
            }
            fprintf(stderr, "\n");
            di += 3 + drl;
        }
    }
    
    /* Parse OMF records */
    i = 0;
    while( i < omf_size && i + 3 <= omf_size ) {
        unsigned char rt = omf[i];
        rec_len = omf[i+1] | (omf[i+2] << 8);
        
        if( rt == OMF_LEDATA32 || rt == OMF_LEDATA ) {
            int seg_idx = omf[i+3];
            int ds, dl;
            if( rt == OMF_LEDATA32 )
                ds = i + 3 + 1 + 4;  /* 1 seg + 4 offset */
            else
                ds = i + 3 + 1 + 2;  /* 1 seg + 2 offset */
            if( seg_idx & 0x80 ) ds++;  /* 2-byte seg index */
            dl = rec_len - (ds - (i+3)) - 1;
            if( dl > 0 && ds + dl <= omf_size ) {
                fprintf(stderr, "LEDATA: rt=%02x seg=%d ds=%d dl=%d first4=",
                        rt, seg_idx, ds-i, dl);
                for(int dbg=0; dbg<4 && dbg<dl; dbg++)
                    fprintf(stderr, "%02x ", omf[ds+dbg]);
                fprintf(stderr, "\n");
                memcpy( code + code_len, omf + ds, dl );
                code_len += dl;
            }
        }
        else if( rt == OMF_EXTDEF && ext_count < 64 ) {
            int j = i + 3, end = i + 3 + rec_len - 1;
            while( j < end && ext_count < 64 ) {
                int nl = omf[j++];
                if( nl > 0 && j + nl <= end ) {
                    char *n = (char *)malloc(nl+1);
                    memcpy(n, omf+j, nl); n[nl] = 0;
                    /* Strip trailing _ */
                    if( nl > 1 && n[nl-1] == '_' ) n[nl-1] = 0;
                    ext_names[ext_count++] = n;
                    j += nl;
                }
                if( j < end ) j++; /* type index */
            }
        }
        i += 3 + rec_len;
    }
    
    /* Write ELF64 directly (like elf64_emit.c) */
    temp_name = (char *)malloc( strlen(obj_filename) + 5 );
    sprintf( temp_name, "%s.tmp", obj_filename );
    fp_out = fopen( temp_name, "wb" );
    if( !fp_out ) { free(omf); free(code); free(temp_name); return; }
    
    /* Build string table */
    /* \0 .text\0 .strtab\0 .symtab\0 main\0 ext1\0 ext2\0 ... */
    char strtab[2048];
    int st_len = 0;
    strtab[st_len++] = 0;  /* null string */
    int str_text = st_len;
    memcpy(strtab+st_len, ".text", 6); st_len += 6;
    int str_strtab = st_len;
    memcpy(strtab+st_len, ".strtab", 8); st_len += 8;
    int str_symtab = st_len;
    memcpy(strtab+st_len, ".symtab", 8); st_len += 8;
    int str_main = st_len;
    memcpy(strtab+st_len, "main", 5); st_len += 5;
    
    int str_ext[64];
    for( int e = 0; e < ext_count; e++ ) {
        str_ext[e] = st_len;
        int el = strlen(ext_names[e]) + 1;
        memcpy(strtab+st_len, ext_names[e], el);
        st_len += el;
    }
    
    /* Layout */
    int num_syms = 3 + ext_count;  /* null + main + section + externals */
    int num_local = 2;  /* null + section */
    size_t text_off = sizeof(Elf64_Ehdr);
    size_t text_pad = (16 - (text_off + code_len) % 16) % 16;
    size_t symtab_off = text_off + code_len + text_pad;
    size_t symtab_sz = num_syms * sizeof(Elf64_Sym);
    size_t strtab_off = symtab_off + symtab_sz;
    size_t shdr_off = strtab_off + st_len;
    size_t shdr_pad = (8 - shdr_off % 8) % 8;
    shdr_off += shdr_pad;
    int num_shdrs = 4;  /* null + .text + .symtab + .strtab */
    
    /* ELF header */
    Elf64_Ehdr ehdr;
    memset(&ehdr, 0, sizeof(ehdr));
    ehdr.e_ident[EI_MAG0] = ELFMAG0;
    ehdr.e_ident[EI_MAG1] = ELFMAG1;
    ehdr.e_ident[EI_MAG2] = ELFMAG2;
    ehdr.e_ident[EI_MAG3] = ELFMAG3;
    ehdr.e_ident[EI_CLASS] = ELFCLASS64;
    ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr.e_ident[EI_VERSION] = EV_CURRENT;
    ehdr.e_ident[EI_OSABI] = ELFOSABI_NONE;
    ehdr.e_type = ET_REL;
    ehdr.e_machine = EM_X86_64;
    ehdr.e_version = EV_CURRENT;
    ehdr.e_shoff = shdr_off;
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_shentsize = sizeof(Elf64_Shdr);
    ehdr.e_shnum = num_shdrs;
    ehdr.e_shstrndx = 3; /* .strtab */
    fwrite(&ehdr, 1, sizeof(ehdr), fp_out);
    
    /* .text content */
    fwrite(code, 1, code_len, fp_out);
    { char pad[16] = {0}; fwrite(pad, 1, text_pad, fp_out); }
    
    /* Symbol table */
    Elf64_Sym sym;
    /* [0] null */
    memset(&sym, 0, sizeof(sym)); fwrite(&sym, 1, sizeof(sym), fp_out);
    /* [1] .text section */
    memset(&sym, 0, sizeof(sym));
    sym.st_name = str_text;
    sym.st_info = ELF64_ST_INFO(STB_LOCAL, STT_SECTION);
    sym.st_shndx = 1;
    fwrite(&sym, 1, sizeof(sym), fp_out);
    /* [2] main — global function at offset 0 */
    memset(&sym, 0, sizeof(sym));
    sym.st_name = str_main;
    sym.st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC);
    sym.st_shndx = 1;  /* .text */
    sym.st_value = 0;
    sym.st_size = code_len;
    fwrite(&sym, 1, sizeof(sym), fp_out);
    /* [3+] externals */
    for( int e = 0; e < ext_count; e++ ) {
        memset(&sym, 0, sizeof(sym));
        sym.st_name = str_ext[e];
        sym.st_info = ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE);
        sym.st_shndx = SHN_UNDEF;
        fwrite(&sym, 1, sizeof(sym), fp_out);
    }
    
    /* String table */
    fwrite(strtab, 1, st_len, fp_out);
    { char pad[8] = {0}; fwrite(pad, 1, shdr_pad, fp_out); }
    
    /* Section headers */
    Elf64_Shdr shdr;
    /* [0] null */
    memset(&shdr, 0, sizeof(shdr)); fwrite(&shdr, 1, sizeof(shdr), fp_out);
    /* [1] .text */
    memset(&shdr, 0, sizeof(shdr));
    shdr.sh_name = str_text;
    shdr.sh_type = SHT_PROGBITS;
    shdr.sh_flags = SHF_ALLOC | SHF_EXECINSTR;
    shdr.sh_offset = text_off;
    shdr.sh_size = code_len;
    shdr.sh_addralign = 16;
    fwrite(&shdr, 1, sizeof(shdr), fp_out);
    /* [2] .symtab */
    memset(&shdr, 0, sizeof(shdr));
    shdr.sh_name = str_symtab;
    shdr.sh_type = SHT_SYMTAB;
    shdr.sh_offset = symtab_off;
    shdr.sh_size = symtab_sz;
    shdr.sh_link = 3;  /* .strtab */
    shdr.sh_info = num_local;
    shdr.sh_addralign = 8;
    shdr.sh_entsize = sizeof(Elf64_Sym);
    fwrite(&shdr, 1, sizeof(shdr), fp_out);
    /* [3] .strtab */
    memset(&shdr, 0, sizeof(shdr));
    shdr.sh_name = str_strtab;
    shdr.sh_type = SHT_STRTAB;
    shdr.sh_offset = strtab_off;
    shdr.sh_size = st_len;
    shdr.sh_addralign = 1;
    fwrite(&shdr, 1, sizeof(shdr), fp_out);
    
    fclose(fp_out);
    
    /* Replace OMF with ELF64 */
    remove(obj_filename);
    rename(temp_name, obj_filename);
    
    for( i = 0; i < ext_count; i++ ) free(ext_names[i]);
    free(code); free(omf); free(temp_name); free(obj_filename);
    x64_active = false;
}

/* Stubs */
void X64OutDBytes(unsigned l, const byte *s) {(void)l;(void)s;}
void X64OutDataByte(byte v) {(void)v;}
void X64OutDataShort(unsigned short v) {(void)v;}
void X64OutDataLong(unsigned long v) {(void)v;}
void X64OutIBytes(byte p, unsigned long l) {(void)p;(void)l;}
void X64OutLabel(const char *n) {(void)n;}
void X64ObjLabel(const char *n, bool g) {(void)n;(void)g;}
void X64SetCodeMode(bool c) {(void)c;}
void X64OutImport(const char *n) {(void)n;}
void X64OutPatchImport(const char *n) {(void)n;}
void X64TrackBytes(unsigned l) {(void)l;}
void X64GenObject(void) {}
