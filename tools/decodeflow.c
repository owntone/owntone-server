/*
 * $Id$
 *
 * Decode a tcpflow from iTunes
 */

#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <zlib.h>

#include <sys/types.h>


typedef struct tag_daap_items {
    int type;
    char *tag;
    char *description;
} DAAP_ITEMS;

DAAP_ITEMS taglist[] = {
    { 0x05, "miid", "dmap.itemid" },
    { 0x09, "minm", "dmap.itemname" },
    { 0x01, "mikd", "dmap.itemkind" },
    { 0x07, "mper", "dmap.persistentid" },
    { 0x0C, "mcon", "dmap.container" },
    { 0x05, "mcti", "dmap.containeritemid" },
    { 0x05, "mpco", "dmap.parentcontainerid" },
    { 0x05, "mstt", "dmap.status" },
    { 0x09, "msts", "dmap.statusstring" },
    { 0x05, "mimc", "dmap.itemcount" },
    { 0x05, "mctc", "dmap.containercount" },
    { 0x05, "mrco", "dmap.returnedcount" },
    { 0x05, "mtco", "dmap.specifiedtotalcount" },
    { 0x0C, "mlcl", "dmap.listing" },
    { 0x0C, "mlit", "dmap.listingitem" },
    { 0x0C, "mbcl", "dmap.bag" },
    { 0x0C, "mdcl", "dmap.dictionary" },
    { 0x0C, "msrv", "dmap.serverinforesponse" },
    { 0x01, "msau", "dmap.authenticationmethod" },
    { 0x01, "mslr", "dmap.loginrequired" },
    { 0x0B, "mpro", "dmap.protocolversion" },
    { 0x01, "msal", "dmap.supportsautologout" },
    { 0x01, "msup", "dmap.supportsupdate" },
    { 0x01, "mspi", "dmap.supportspersistentids" },
    { 0x01, "msex", "dmap.supportsextensions" },
    { 0x01, "msbr", "dmap.supportsbrowse" },
    { 0x01, "msqy", "dmap.supportsquery" },
    { 0x01, "msix", "dmap.supportsindex" },
    { 0x01, "msrs", "dmap.supportsresolve" },
    { 0x05, "mstm", "dmap.timeoutinterval" },
    { 0x05, "msdc", "dmap.databasescount" },
    { 0x0C, "mlog", "dmap.loginresponse" },
    { 0x05, "mlid", "dmap.sessionid" },
    { 0x0C, "mupd", "dmap.updateresponse" },
    { 0x05, "musr", "dmap.serverrevision" },
    { 0x01, "muty", "dmap.updatetype" },
    { 0x0C, "mudl", "dmap.deletedidlisting" },
    { 0x0C, "mccr", "dmap.contentcodesresponse" },
    { 0x05, "mcnm", "dmap.contentcodesnumber" },
    { 0x09, "mcna", "dmap.contentcodesname" },
    { 0x03, "mcty", "dmap.contentcodestype" },
    { 0x0B, "apro", "daap.protocolversion" },
    { 0x0C, "avdb", "daap.serverdatabases" },
    { 0x0C, "abro", "daap.databasebrowse" },
    { 0x0C, "abal", "daap.browsealbumlisting" },
    { 0x0C, "abar", "daap.browseartistlisting" },
    { 0x0C, "abcp", "daap.browsecomposerlisting" },
    { 0x0C, "abgn", "daap.browsegenrelisting" },
    { 0x0C, "adbs", "daap.databasesongs" },
    { 0x09, "asal", "daap.songalbum" },
    { 0x09, "asar", "daap.songartist" },
    { 0x03, "asbt", "daap.songbeatsperminute" },
    { 0x03, "asbr", "daap.songbitrate" },
    { 0x09, "ascm", "daap.songcomment" },
    { 0x01, "asco", "daap.songcompilation" },
    { 0x09, "ascp", "daap.songcomposer" },
    { 0x0A, "asda", "daap.songdateadded" },
    { 0x0A, "asdm", "daap.songdatemodified" },
    { 0x03, "asdc", "daap.songdisccount" },
    { 0x03, "asdn", "daap.songdiscnumber" },
    { 0x01, "asdb", "daap.songdisabled" },
    { 0x09, "aseq", "daap.songeqpreset" },
    { 0x09, "asfm", "daap.songformat" },
    { 0x09, "asgn", "daap.songgenre" },
    { 0x09, "asdt", "daap.songdescription" },
    { 0x02, "asrv", "daap.songrelativevolume" },
    { 0x05, "assr", "daap.songsamplerate" },
    { 0x05, "assz", "daap.songsize" },
    { 0x05, "asst", "daap.songstarttime" },
    { 0x05, "assp", "daap.songstoptime" },
    { 0x05, "astm", "daap.songtime" },
    { 0x03, "astc", "daap.songtrackcount" },
    { 0x03, "astn", "daap.songtracknumber" },
    { 0x01, "asur", "daap.songuserrating" },
    { 0x03, "asyr", "daap.songyear" },
    { 0x01, "asdk", "daap.songdatakind" },
    { 0x09, "asul", "daap.songdataurl" },
    { 0x0C, "aply", "daap.databaseplaylists" },
    { 0x01, "abpl", "daap.baseplaylist" },
    { 0x0C, "apso", "daap.playlistsongs" },
    { 0x0C, "arsv", "daap.resolve" },
    { 0x0C, "arif", "daap.resolveinfo" },
    { 0x05, "aeNV", "com.apple.itunes.norm-volume" },
    { 0x01, "aeSP", "com.apple.itunes.smart-playlist" },
    /* iTunes 4.5+ */
    { 0x01, "msas", "dmap.authenticationschemes" },
    { 0x05, "ascd", "daap.songcodectype" },
    { 0x05, "ascs", "daap.songcodecsubtype" },
    { 0x09, "agrp", "daap.songgrouping" },
    { 0x05, "aeSV", "com.apple.itunes.music-sharing-version" },
    { 0x05, "aePI", "com.apple.itunes.itms-playlistid" },
    { 0x05, "aeCI", "com.apple.iTunes.itms-composerid" },
    { 0x05, "aeGI", "com.apple.iTunes.itms-genreid" },
    { 0x05, "aeAI", "com.apple.iTunes.itms-artistid" },
    { 0x05, "aeSI", "com.apple.iTunes.itms-songid" },
    { 0x05, "aeSF", "com.apple.iTunes.itms-storefrontid" },

    /* iTunes 5.0+ */
    { 0x01, "ascr", "daap.songcontentrating" },
    { 0x01, "f" "\x8d" "ch", "dmap.haschildcontainers" }, /* was 5? */

    /* iTunes 6.0.2+ */
    { 0x01, "aeHV", "com.apple.itunes.has-video" },

    /* iTunes 6.0.4+ */
    { 0x05, "msas", "dmap.authenticationschemes" },
    { 0x09, "asct", "daap.songcategory" },
    { 0x09, "ascn", "daap.songcontentdescription" },
    { 0x09, "aslc", "daap.songlongcontentdescription" },
    { 0x09, "asky", "daap.songkeywords" },
    { 0x01, "apsm", "daap.playlistshufflemode" },
    { 0x01, "aprm", "daap.playlistrepeatmode" },
    { 0x01, "aePC", "com.apple.itunes.is-podcast" },
    { 0x01, "aePP", "com.apple.itunes.is-podcast-playlist" },
    { 0x01, "aeMK", "com.apple.itunes.mediakind" },
    { 0x09, "aeSN", "com.apple.itunes.series-name" },
    { 0x09, "aeNN", "com.apple.itunes.network-name" },
    { 0x09, "aeEN", "com.apple.itunes.episode-num-str" },
    { 0x05, "aeES", "com.apple.itunes.episode-sort" },
    { 0x05, "aeSU", "com.apple.itunes.season-num" },

    /* mt-daapd specific */
    { 0x09, "MSPS", "org.mt-daapd.smart-playlist-spec" },
    { 0x01, "MPTY", "org.mt-daapd.playlist-type" },
    { 0x0C, "MAPR", "org.mt-daapd.addplaylist" },
    { 0x0C, "MAPI", "org.mt-daapd.addplaylistitem" },
    { 0x0C, "MDPR", "org.mt-daapd.delplaylist" },
    { 0x0C, "MDPI", "org.mt-daapd.delplaylistitem" },
    { 0x0C, "MEPR", "org.mt-daapd.editplaylist" },
    { 0x00, NULL,   NULL }
};

int lookup_tag(char *tag, char *descr, int *type) {
    DAAP_ITEMS *current = taglist;

    while((current->tag) && (strcasecmp(current->tag,tag))) {
        current++;
    }

    if(current->tag) {
        strcpy(descr,current->description);
        *type = current->type;
        return 1;
    }

    return 0;
}

int decode_tag(FILE *fout, unsigned char *current, int level, int len) {
    int type;
    int subtag_len;
    int tempint;

    char tag[5];
    char descr[40];
    char line[4096];
    char templine[4096];

    unsigned char cval;
    unsigned short int sival;
    unsigned int ival;
    unsigned long long lival;

    while(len) {
        memset(tag,0,sizeof(tag));
        memcpy(tag,current,4);
        current += 4;
        len -= 4;

        subtag_len = current[0] << 24 |
            current[1] << 16 |
            current[2] << 8 |
            current[3];

        fprintf(stderr,"Tag: %c%c%c%c, subtag len: %d, len: %d\n",
                tag[0],tag[1],tag[2],tag[3],subtag_len, len);
        current += 4;
        len -= 4;

        if(lookup_tag(tag,descr,&type)) {
            memset(line,' ',sizeof(line));
            sprintf((char*)&line[level * 2],"%02x %s (%s) - ",
                    type,tag,descr);

            switch(type) {
            case 0x01: /* byte */
            case 0x02: /* unsigned byte */
                if(subtag_len != 1) {
                    printf("Foo!  %s should have tag len 1, has %d\n",
                           tag,subtag_len);
                    exit(EXIT_FAILURE);
                }
                cval = *current;
                sprintf(templine,"%02x (%d)\n",cval, (int)cval);
                current += 1;
                len -= 1;
                break;

            case 0x03:
                if(subtag_len != 2) {
                    printf("Foo!  %s should have tag len 2, has %d\n",
                           tag,subtag_len);
                    exit(EXIT_FAILURE);
                }
                sival = current[0] << 8 |
                    current[1];
                sprintf(templine,"%02x %d\n",sival,(int)sival);
                current += 2;
                len -= 2;
                break;

                break;

            case 0x0A:
            case 0x05:
                if(subtag_len != 4) {
                    printf("Foo!  %s should have tag len 4, has %d\n",
                           tag,subtag_len);
                    exit(EXIT_FAILURE);
                }

                if(strcmp(tag,"mcnm") == 0) {
                    sprintf(templine,"%c%c%c%c (%04x)\n",
                            *((char*)current),
                            *((char*)current+1),
                            *((char*)current+2),
                            *((char*)current+3),
                            *((int*)current));
                } else {
                    ival = current[0] << 24 |
                        current[1] << 16 |
                        current[2] << 8 |
                        current[3];
                    sprintf(templine,"%04x (%d)\n",ival,ival);
                }
                current += 4;
                len -= 4;
                break;

            case 0x07:
                if(subtag_len != 8) {
                    printf("Foo!  %s should have tag len 8, has %d\n",
                           tag,subtag_len);
                    exit(EXIT_FAILURE);
                }

                lival = current[0] << 24 |
                    current[1] << 16 |
                    current[2] << 8 |
                    current[3];

                lival = lival << 32;
                lival = lival | current [4] << 24 |
                    current[5] << 16 |
                    current[6] << 8 |
                    current[7];

                sprintf(templine,"%04x%04x (%lu)\n",
                        (unsigned int)((lival >> 32) & 0xFFFFFFFF),
                        (unsigned int)(lival & 0xFFFFFFFF),
                        lival);
                current += 8;
                len -= 8;
                break;

            case 0x09:
                if(subtag_len == 0) {
                    strcpy(templine,"(empty)\n");
                } else {
                    memset(templine,0,sizeof(templine));
                    memcpy(templine,current,subtag_len);
                    strcat(templine,"\n");
                }
                current += subtag_len;
                len -= subtag_len;
                break;

            case 0x0B:
                if(subtag_len != 4) {
                    printf("Foo!  %s should have tag len 4, has %d\n",
                           tag,subtag_len);
                    exit(EXIT_FAILURE);
                }

                ival = current[0] << 24 |
                    current[1] << 16 |
                    current[2] << 8 |
                    current[3];
                sprintf(templine,"%d.%d\n",ival >> 16 & 0xFFFF,
                        ival & 0xFFFF);

                current += 4;
                len -= 4;
                break;

            case 0x0C:
                sprintf(templine,"<container>\n");
                break;

            default:
                printf("Foo!  Bad tag: type %d\n",type);
                exit(EXIT_FAILURE);
            }


            fprintf(fout,"%s%s",line,templine);

            if(type == 0x0c) {
                if(decode_tag(fout, current,level+1,subtag_len)) {
                    current += subtag_len;
                    len -= subtag_len;
                } else {
                    return 0;
                }
            }
        } else {
            printf("Bad tag: %s (%02x%02x%02x%02x)\n",tag,(unsigned char)tag[0],(unsigned char)tag[1],
                   (unsigned char)tag[2],(unsigned char)tag[3]);
            exit(EXIT_FAILURE);
            return 0;
        }

    }

    return 1;
}



void decode_dmap(int conv, unsigned char *uncompressed, long uncompressed_size) {
    char buffer[256];
    FILE *fout;

    sprintf(buffer,"decoded.%d",conv);
    fout=fopen(buffer,"w");

    if(!decode_tag(fout,uncompressed,0,uncompressed_size)) {
        printf("Foo!  All screwed up!\n");
    }
    fclose(fout);
}


int readline(int fd, char *buffer) {
    char *current;
    char inchar;

    current=buffer;
    while(read(fd,&inchar,1) == 1) {
        switch(inchar) {
        case '\r':
            break;
        case '\n':
            *current='\0';
            return 0;
        default:
            *current++ = inchar;
            break;
        }
    }

    return -1;
}


void usage(void) {
    fprintf(stderr,"usage:  decodeflow [-d] file\n");
    fprintf(stderr,"   -d:     file is a dmap dump, not a flow");
    fprintf(stderr,"\n\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    int fd;
    int conversation=0;
    char buffer[1024];
    int done=0;
    FILE *out;
    char *compressed;
    unsigned char *uncompressed;
    long compressed_size;
    long uncompressed_size;
    int err;
    char file[256];
    int out_fd;
    FILE *stream;
    int option;
    int is_compressed;
    char *loc;
    int dmap=0;

    while((option=getopt(argc, argv, "d")) != -1) {
        switch(option) {
        case 'd':
            dmap=1;
            break;
        default:
            usage();
            break;
        }
    }


    if(optind == argc) {
        usage();
    }

    fd=open(argv[optind],O_RDONLY);
    if(fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    if(dmap) {
        out=fdopen(STDOUT_FILENO,"w");
        uncompressed_size=lseek(fd,0,SEEK_END);
        lseek(fd,0,SEEK_SET);
        uncompressed=(unsigned char*)malloc(uncompressed_size);
        read(fd,uncompressed,uncompressed_size);
        decode_tag(out,uncompressed,0,uncompressed_size);
        exit(EXIT_SUCCESS);
    }


    while(!done) {
        /* read in the headers */
        is_compressed=0;
        printf("Reading headers for conv %d\n",conversation);
        while(1) {
            if(readline(fd,buffer)) {
                done=1;
                break;
            }

            printf("got %s\n",buffer);

            if(!strlen(buffer))
                break;

            if(strncasecmp(buffer,"Content-Encoding:",17) == 0) {
                loc=buffer+17;
                while(*loc==' ') {
                    loc++;
                }

                if(strncasecmp(loc,"gzip",4) == 0) {
                    is_compressed=1;
                }
            }

            if(strncasecmp(buffer,"Content-Length:",15) == 0) {
                compressed_size=atol((char*)&buffer[15]);
                printf("Size of conv %d is %d\n",conversation,compressed_size);
            }
        }

        if(done)
            break;

        printf("Headers complete for conversation %d\n",conversation);
        printf("Flow %s compressed\n",is_compressed ? "IS" : "IS NOT");

        uncompressed_size = 20 * compressed_size;

        compressed=(char*)malloc(compressed_size);
        uncompressed=(unsigned char*)malloc(uncompressed_size);

        if((!compressed) || (!uncompressed)) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }

        if(read(fd,compressed,compressed_size) != compressed_size) {
            perror("read");
            exit(EXIT_FAILURE);
        }

        /* dump the compressed data */
        sprintf(file,"compressed.%d",conversation);
        out_fd=open(file,O_CREAT | O_RDWR,0666);
        if(out_fd == -1) {
            perror("open");
            exit(EXIT_FAILURE);
        }

        write(out_fd,compressed,compressed_size);
        close(out_fd);

        if(is_compressed) {
            sprintf(file,"/sw/bin/zcat compressed.%d",conversation);
            stream=popen(file,"r");
            if(!stream) {
                perror("popen");
                exit(EXIT_FAILURE);
            }


            err=fread(uncompressed,1,uncompressed_size,stream);
            if(err == -1) {
                perror("fread");
                exit(EXIT_FAILURE);
            }

            if(err == uncompressed_size) {
                printf("Error: buffer too small\n");
                exit(EXIT_FAILURE);
            }

            uncompressed_size = err;
            pclose(stream);
        } else {
            uncompressed_size=compressed_size;
            memcpy(uncompressed,compressed,compressed_size);
        }

        /* dump the uncompressed data */
        sprintf(file,"uncompressed.%d",conversation);
        out_fd=open(file,O_CREAT | O_RDWR,0666);
        if(out_fd == -1) {
            perror("open");
            exit(EXIT_FAILURE);
        }

        write(out_fd,uncompressed,uncompressed_size);
        close(out_fd);

        printf("Uncompressed size: %d\n",uncompressed_size);

        /* now decode and print */
        decode_dmap(conversation, uncompressed, uncompressed_size);

        free(compressed);
        free(uncompressed);
        conversation++;
    }

    printf("Done");
}


