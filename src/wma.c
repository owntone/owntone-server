/*
 * $ Id: $
 * WMA metatag parsing
 *
 * Copyright (C) 2005 Ron Pedde (ron@pedde.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <fcntl.h>
#include <stdio.h>
#include <errno.h>

#include "mp3-scanner.h"
#include "restart.h"
#include "err.h"

typedef struct tag_wma_guidlist {
    char *name;
    char *guid;
    char value[16];
} WMA_GUID;

WMA_GUID wma_guidlist[] = {
    { "ASF_Index_Object",
      "D6E229D3-35DA-11D1-9034-00A0C90349BE",
      "\xD3\x29\xE2\xD6\xDA\x35\xD1\x11\x90\x34\x00\xA0\xC9\x03\x49\xBE" },
    { "ASF_Extended_Stream_Properties_Object",
      "14E6A5CB-C672-4332-8399-A96952065B5A",
      "\xCB\xA5\xE6\x14\x72\xC6\x32\x43\x83\x99\xA9\x69\x52\x06\x5B\x5A" },
    { "ASF_Payload_Ext_Syst_Pixel_Aspect_Ratio",
      "1B1EE554-F9EA-4BC8-821A-376B74E4C4B8",
      "\x54\xE5\x1E\x1B\xEA\xF9\xC8\x4B\x82\x1A\x37\x6B\x74\xE4\xC4\xB8" },
    { "ASF_Bandwidth_Sharing_Object",
      "A69609E6-517B-11D2-B6AF-00C04FD908E9",
      "\xE6\x09\x96\xA6\x7B\x51\xD2\x11\xB6\xAF\x00\xC0\x4F\xD9\x08\xE9" },
    { "ASF_Payload_Extension_System_Timecode",
      "399595EC-8667-4E2D-8FDB-98814CE76C1E",
      "\xEC\x95\x95\x39\x67\x86\x2D\x4E\x8F\xDB\x98\x81\x4C\xE7\x6C\x1E" },
    { "ASF_Marker_Object",
      "F487CD01-A951-11CF-8EE6-00C00C205365",
      "\x01\xCD\x87\xF4\x51\xA9\xCF\x11\x8E\xE6\x00\xC0\x0C\x20\x53\x65" },
    { "ASF_Data_Object",
      "75B22636-668E-11CF-A6D9-00AA0062CE6C",
      "\x36\x26\xB2\x75\x8E\x66\xCF\x11\xA6\xD9\x00\xAA\x00\x62\xCE\x6C" },
    { "ASF_Content_Description_Object",
      "75B22633-668E-11CF-A6D9-00AA0062CE6C",
      "\x33\x26\xB2\x75\x8E\x66\xCF\x11\xA6\xD9\x00\xAA\x00\x62\xCE\x6C" },
    { "ASF_Reserved_1",
      "ABD3D211-A9BA-11cf-8EE6-00C00C205365",
      "\x11\xD2\xD3\xAB\xBA\xA9\xcf\x11\x8E\xE6\x00\xC0\x0C\x20\x53\x65" },
    { "ASF_Timecode_Index_Object",
      "3CB73FD0-0C4A-4803-953D-EDF7B6228F0C",
      "\xD0\x3F\xB7\x3C\x4A\x0C\x03\x48\x95\x3D\xED\xF7\xB6\x22\x8F\x0C" },
    { "ASF_Language_List_Object",
      "7C4346A9-EFE0-4BFC-B229-393EDE415C85",
      "\xA9\x46\x43\x7C\xE0\xEF\xFC\x4B\xB2\x29\x39\x3E\xDE\x41\x5C\x85" },
    { "ASF_No_Error_Correction",
      "20FB5700-5B55-11CF-A8FD-00805F5C442B",
      "\x00\x57\xFB\x20\x55\x5B\xCF\x11\xA8\xFD\x00\x80\x5F\x5C\x44\x2B" },
    { "ASF_Extended_Content_Description_Object",
      "D2D0A440-E307-11D2-97F0-00A0C95EA850",
      "\x40\xA4\xD0\xD2\x07\xE3\xD2\x11\x97\xF0\x00\xA0\xC9\x5E\xA8\x50" },
    { "ASF_Media_Object_Index_Parameters_Obj",
      "6B203BAD-3F11-4E84-ACA8-D7613DE2CFA7",
      "\xAD\x3B\x20\x6B\x11\x3F\x84\x4E\xAC\xA8\xD7\x61\x3D\xE2\xCF\xA7" },
    { "ASF_Codec_List_Object",
      "86D15240-311D-11D0-A3A4-00A0C90348F6",
      "\x40\x52\xD1\x86\x1D\x31\xD0\x11\xA3\xA4\x00\xA0\xC9\x03\x48\xF6" },
    { "ASF_Stream_Bitrate_Properties_Object",
      "7BF875CE-468D-11D1-8D82-006097C9A2B2",
      "\xCE\x75\xF8\x7B\x8D\x46\xD1\x11\x8D\x82\x00\x60\x97\xC9\xA2\xB2" },
    { "ASF_Script_Command_Object",
      "1EFB1A30-0B62-11D0-A39B-00A0C90348F6",
      "\x30\x1A\xFB\x1E\x62\x0B\xD0\x11\xA3\x9B\x00\xA0\xC9\x03\x48\xF6" },
    { "ASF_Degradable_JPEG_Media",
      "35907DE0-E415-11CF-A917-00805F5C442B",
      "\xE0\x7D\x90\x35\x15\xE4\xCF\x11\xA9\x17\x00\x80\x5F\x5C\x44\x2B" },
    { "ASF_Header_Object",
      "75B22630-668E-11CF-A6D9-00AA0062CE6C",
      "\x30\x26\xB2\x75\x8E\x66\xCF\x11\xA6\xD9\x00\xAA\x00\x62\xCE\x6C" },
    { "ASF_Padding_Object",
      "1806D474-CADF-4509-A4BA-9AABCB96AAE8",
      "\x74\xD4\x06\x18\xDF\xCA\x09\x45\xA4\xBA\x9A\xAB\xCB\x96\xAA\xE8" },
    { "ASF_JFIF_Media",
      "B61BE100-5B4E-11CF-A8FD-00805F5C442B",
      "\x00\xE1\x1B\xB6\x4E\x5B\xCF\x11\xA8\xFD\x00\x80\x5F\x5C\x44\x2B" },
    { "ASF_Digital_Signature_Object",
      "2211B3FC-BD23-11D2-B4B7-00A0C955FC6E",
      "\xFC\xB3\x11\x22\x23\xBD\xD2\x11\xB4\xB7\x00\xA0\xC9\x55\xFC\x6E" },
    { "ASF_Metadata_Library_Object",
      "44231C94-9498-49D1-A141-1D134E457054",
      "\x94\x1C\x23\x44\x98\x94\xD1\x49\xA1\x41\x1D\x13\x4E\x45\x70\x54" },
    { "ASF_Payload_Ext_System_File_Name",
      "E165EC0E-19ED-45D7-B4A7-25CBD1E28E9B",
      "\x0E\xEC\x65\xE1\xED\x19\xD7\x45\xB4\xA7\x25\xCB\xD1\xE2\x8E\x9B" },
    { "ASF_Stream_Prioritization_Object",
      "D4FED15B-88D3-454F-81F0-ED5C45999E24",
      "\x5B\xD1\xFE\xD4\xD3\x88\x4F\x45\x81\xF0\xED\x5C\x45\x99\x9E\x24" },
    { "ASF_Bandwidth_Sharing_Exclusive",
      "AF6060AA-5197-11D2-B6AF-00C04FD908E9",
      "\xAA\x60\x60\xAF\x97\x51\xD2\x11\xB6\xAF\x00\xC0\x4F\xD9\x08\xE9" },
    { "ASF_Group_Mutual_Exclusion_Object",
      "D1465A40-5A79-4338-B71B-E36B8FD6C249",
      "\x40\x5A\x46\xD1\x79\x5A\x38\x43\xB7\x1B\xE3\x6B\x8F\xD6\xC2\x49" },
    { "ASF_Audio_Spread",
      "BFC3CD50-618F-11CF-8BB2-00AA00B4E220",
      "\x50\xCD\xC3\xBF\x8F\x61\xCF\x11\x8B\xB2\x00\xAA\x00\xB4\xE2\x20" },
    { "ASF_Advanced_Mutual_Exclusion_Object",
      "A08649CF-4775-4670-8A16-6E35357566CD",
      "\xCF\x49\x86\xA0\x75\x47\x70\x46\x8A\x16\x6E\x35\x35\x75\x66\xCD" },
    { "ASF_Payload_Ext_Syst_Sample_Duration",
      "C6BD9450-867F-4907-83A3-C77921B733AD",
      "\x50\x94\xBD\xC6\x7F\x86\x07\x49\x83\xA3\xC7\x79\x21\xB7\x33\xAD" },
    { "ASF_Stream_Properties_Object",
      "B7DC0791-A9B7-11CF-8EE6-00C00C205365",
      "\x91\x07\xDC\xB7\xB7\xA9\xCF\x11\x8E\xE6\x00\xC0\x0C\x20\x53\x65" },
    { "ASF_Metadata_Object",
      "C5F8CBEA-5BAF-4877-8467-AA8C44FA4CCA",
      "\xEA\xCB\xF8\xC5\xAF\x5B\x77\x48\x84\x67\xAA\x8C\x44\xFA\x4C\xCA" },
    { "ASF_Mutex_Unknown",
      "D6E22A02-35DA-11D1-9034-00A0C90349BE",
      "\x02\x2A\xE2\xD6\xDA\x35\xD1\x11\x90\x34\x00\xA0\xC9\x03\x49\xBE" },
    { "ASF_Content_Branding_Object",
      "2211B3FA-BD23-11D2-B4B7-00A0C955FC6E",
      "\xFA\xB3\x11\x22\x23\xBD\xD2\x11\xB4\xB7\x00\xA0\xC9\x55\xFC\x6E" },
    { "ASF_Content_Encryption_Object",
      "2211B3FB-BD23-11D2-B4B7-00A0C955FC6E",
      "\xFB\xB3\x11\x22\x23\xBD\xD2\x11\xB4\xB7\x00\xA0\xC9\x55\xFC\x6E" },
    { "ASF_Index_Parameters_Object",
      "D6E229DF-35DA-11D1-9034-00A0C90349BE",
      "\xDF\x29\xE2\xD6\xDA\x35\xD1\x11\x90\x34\x00\xA0\xC9\x03\x49\xBE" },
    { "ASF_Payload_Ext_System_Content_Type",
      "D590DC20-07BC-436C-9CF7-F3BBFBF1A4DC",
      "\x20\xDC\x90\xD5\xBC\x07\x6C\x43\x9C\xF7\xF3\xBB\xFB\xF1\xA4\xDC" },
    { "ASF_Web_Stream_Media_Subtype",
      "776257D4-C627-41CB-8F81-7AC7FF1C40CC",
      "\xD4\x57\x62\x77\x27\xC6\xCB\x41\x8F\x81\x7A\xC7\xFF\x1C\x40\xCC" },
    { "ASF_Web_Stream_Format",
      "DA1E6B13-8359-4050-B398-388E965BF00C",
      "\x13\x6B\x1E\xDA\x59\x83\x50\x40\xB3\x98\x38\x8E\x96\x5B\xF0\x0C" },
    { "ASF_Simple_Index_Object",
      "33000890-E5B1-11CF-89F4-00A0C90349CB",
      "\x90\x08\x00\x33\xB1\xE5\xCF\x11\x89\xF4\x00\xA0\xC9\x03\x49\xCB" },
    { "ASF_Error_Correction_Object",
      "75B22635-668E-11CF-A6D9-00AA0062CE6C",
      "\x35\x26\xB2\x75\x8E\x66\xCF\x11\xA6\xD9\x00\xAA\x00\x62\xCE\x6C" },
    { "ASF_Media_Object_Index_Object",
      "FEB103F8-12AD-4C64-840F-2A1D2F7AD48C",
      "\xF8\x03\xB1\xFE\xAD\x12\x64\x4C\x84\x0F\x2A\x1D\x2F\x7A\xD4\x8C" },
    { "ASF_Mutex_Language",
      "D6E22A00-35DA-11D1-9034-00A0C90349BE",
      "\x00\x2A\xE2\xD6\xDA\x35\xD1\x11\x90\x34\x00\xA0\xC9\x03\x49\xBE" },
    { "ASF_File_Transfer_Media",
      "91BD222C-F21C-497A-8B6D-5AA86BFC0185",
      "\x2C\x22\xBD\x91\x1C\xF2\x7A\x49\x8B\x6D\x5A\xA8\x6B\xFC\x01\x85" },
    { "ASF_Reserved_3",
      "4B1ACBE3-100B-11D0-A39B-00A0C90348F6",
      "\xE3\xCB\x1A\x4B\x0B\x10\xD0\x11\xA3\x9B\x00\xA0\xC9\x03\x48\xF6" },
    { "ASF_Bitrate_Mutual_Exclusion_Object",
      "D6E229DC-35DA-11D1-9034-00A0C90349BE",
      "\xDC\x29\xE2\xD6\xDA\x35\xD1\x11\x90\x34\x00\xA0\xC9\x03\x49\xBE" },
    { "ASF_Bandwidth_Sharing_Partial",
      "AF6060AB-5197-11D2-B6AF-00C04FD908E9",
      "\xAB\x60\x60\xAF\x97\x51\xD2\x11\xB6\xAF\x00\xC0\x4F\xD9\x08\xE9" },
    { "ASF_Command_Media",
      "59DACFC0-59E6-11D0-A3AC-00A0C90348F6",
      "\xC0\xCF\xDA\x59\xE6\x59\xD0\x11\xA3\xAC\x00\xA0\xC9\x03\x48\xF6" },
    { "ASF_Audio_Media",
      "F8699E40-5B4D-11CF-A8FD-00805F5C442B",
      "\x40\x9E\x69\xF8\x4D\x5B\xCF\x11\xA8\xFD\x00\x80\x5F\x5C\x44\x2B" },
    { "ASF_Reserved_2",
      "86D15241-311D-11D0-A3A4-00A0C90348F6",
      "\x41\x52\xD1\x86\x1D\x31\xD0\x11\xA3\xA4\x00\xA0\xC9\x03\x48\xF6" },
    { "ASF_Binary_Media",
      "3AFB65E2-47EF-40F2-AC2C-70A90D71D343",
      "\xE2\x65\xFB\x3A\xEF\x47\xF2\x40\xAC\x2C\x70\xA9\x0D\x71\xD3\x43" },
    { "ASF_Mutex_Bitrate",
      "D6E22A01-35DA-11D1-9034-00A0C90349BE",
      "\x01\x2A\xE2\xD6\xDA\x35\xD1\x11\x90\x34\x00\xA0\xC9\x03\x49\xBE" },
    { "ASF_Reserved_4",
      "4CFEDB20-75F6-11CF-9C0F-00A0C90349CB",
      "\x20\xDB\xFE\x4C\xF6\x75\xCF\x11\x9C\x0F\x00\xA0\xC9\x03\x49\xCB" },
    { "ASF_Alt_Extended_Content_Encryption_Obj",
      "FF889EF1-ADEE-40DA-9E71-98704BB928CE",
      "\xF1\x9E\x88\xFF\xEE\xAD\xDA\x40\x9E\x71\x98\x70\x4B\xB9\x28\xCE" },
    { "ASF_Timecode_Index_Parameters_Object",
      "F55E496D-9797-4B5D-8C8B-604DFE9BFB24",
      "\x6D\x49\x5E\xF5\x97\x97\x5D\x4B\x8C\x8B\x60\x4D\xFE\x9B\xFB\x24" },
    { "ASF_Header_Extension_Object",
      "5FBF03B5-A92E-11CF-8EE3-00C00C205365",
      "\xB5\x03\xBF\x5F\x2E\xA9\xCF\x11\x8E\xE3\x00\xC0\x0C\x20\x53\x65" },
    { "ASF_Video_Media",
      "BC19EFC0-5B4D-11CF-A8FD-00805F5C442B",
      "\xC0\xEF\x19\xBC\x4D\x5B\xCF\x11\xA8\xFD\x00\x80\x5F\x5C\x44\x2B" },
    { "ASF_Extended_Content_Encryption_Object",
      "298AE614-2622-4C17-B935-DAE07EE9289C",
      "\x14\xE6\x8A\x29\x22\x26\x17\x4C\xB9\x35\xDA\xE0\x7E\xE9\x28\x9C" },
    { "ASF_File_Properties_Object",
      "8CABDCA1-A947-11CF-8EE4-00C00C205365",
      "\xA1\xDC\xAB\x8C\x47\xA9\xCF\x11\x8E\xE4\x00\xC0\x0C\x20\x53\x65" },
    { NULL, NULL, "\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0" }
};

typedef struct tag_wma_header {
    unsigned char objectid[16];
    unsigned long long size;
    unsigned int objects;
    char reserved1;
    char reserved2;
} __attribute__((packed)) WMA_HEADER;


typedef struct tag_wma_subheader {
    unsigned char objectid[16];
    long size;
} __attribute__((packed)) WMA_SUBHEADER;

WMA_GUID *wma_find_guid(char *guid) {
    WMA_GUID *pguid = wma_guidlist;

    while((pguid->name) && (memcmp(guid,pguid->value,16) != 0)) {
	pguid++;
    }

    if(!pguid->name)
	return NULL;

    return pguid;
}

int scan_get_wmainfo(char *filename, MP3FILE *pmp3) {
    int wma_fd;
    WMA_HEADER hdr;
    WMA_SUBHEADER subhdr;
    WMA_GUID *pguid;
    long offset=0;
    int item;
    unsigned int tmp_hi, tmp_lo;
    unsigned char *convert;
    int err;

    wma_fd = r_open2(filename,O_RDONLY);
    if(wma_fd == -1) {
	DPRINTF(E_INF,L_SCAN,"Error opening WMA file (%s): %s\n",filename,
		strerror(errno));
	return -1;
    }

    if(read(wma_fd,(void*)&hdr,sizeof(hdr)) != sizeof(hdr)) {
	DPRINTF(E_INF,L_SCAN,"Error reading from %s: %s\n",filename,
		strerror(errno));
	r_close(wma_fd);
	return -1;
    }

    DPRINTF(E_DBG,L_SCAN,"Got ObjectID: %02hhx%02hhx%02hhx%02hhx-"
	    "%02hhx%02hhx-%02hhx%02hhx-%02hhx%02hhx-"
	    "%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx\n",
	    hdr.objectid[3],hdr.objectid[2],
	    hdr.objectid[1],hdr.objectid[0],
	    hdr.objectid[5],hdr.objectid[4],
	    hdr.objectid[7],hdr.objectid[6],
	    hdr.objectid[8],hdr.objectid[9],
	    hdr.objectid[10],hdr.objectid[11],
	    hdr.objectid[12],hdr.objectid[13],
	    hdr.objectid[14],hdr.objectid[15]);

    pguid = wma_find_guid(hdr.objectid);
    if(!pguid) {
	DPRINTF(E_INF,L_SCAN,"Could not find header in %s\n",filename);
	r_close(wma_fd);
	return -1;
    }


    convert=(char*)&hdr.size;
    DPRINTF(E_DBG,L_SCAN,"Size: %02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx\n",
	    convert[0],convert[1],convert[2],convert[3],convert[4],
	    convert[5],convert[6],convert[7]);

    tmp_hi = convert[7] << 24 |
	convert[6] << 16 |
	convert[5] << 8 |
	convert[4];

    tmp_lo = convert[3] << 24 |
	convert[2] << 16 |
	convert[1] << 8 |
	convert[0];

    printf("%08x:%08x (%d, %d)\n",tmp_hi,tmp_lo,tmp_hi,tmp_lo);

    hdr.size = tmp_hi;
    hdr.size = (hdr.size << 32) | tmp_lo;

    convert = (char*)&hdr.objects;
    hdr.objects = convert[3] << 24 |
	convert[2] << 16 |
	convert[1] << 8 |
	convert[0];

    DPRINTF(E_DBG,L_SCAN,"Found WMA header: %s\n",pguid->name);
    DPRINTF(E_DBG,L_SCAN,"Header size:      %lld\n",hdr.size);
    DPRINTF(E_DBG,L_SCAN,"Header objects:   %d\n",hdr.objects);

    offset = sizeof(hdr); //hdr.size;

    /* Now we just walk through all the headers and see if we
     * find anything interesting
     */

    
    for(item=0; item < hdr.objects; item++) {
	if(lseek(wma_fd,offset,SEEK_SET) == (off_t)-1) {
	    DPRINTF(E_INF,L_SCAN,"Error seeking in %s\n",filename);
	    r_close(wma_fd);
	    return -1;
	}

	if(r_read(wma_fd,(void*)&subhdr,sizeof(subhdr)) != sizeof(subhdr)) {
	    err=errno;
	    DPRINTF(E_INF,L_SCAN,"Error reading from %s: %s\n",filename,
		    strerror(err));
	    r_close(wma_fd);
	    return -1;
	}

	pguid = wma_find_guid(subhdr.objectid);
	if(pguid) {
	    DPRINTF(E_DBG,L_SCAN,"Found subheader: %s\n",pguid->name);
	    if(strcmp(pguid->name,"ASF_File_Properties_Object")==0) {
		/* parse for more info */
	    }
	} else {
	    DPRINTF(E_DBG,L_SCAN,"Unknown subheader: %02hhx%02hhx%02hhx%02hhx-"
	    "%02hhx%02hhx-%02hhx%02hhx-%02hhx%02hhx-"
	    "%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx\n",
	    subhdr.objectid[3],subhdr.objectid[2],
	    subhdr.objectid[1],subhdr.objectid[0],
	    subhdr.objectid[5],subhdr.objectid[4],
	    subhdr.objectid[7],subhdr.objectid[6],
	    subhdr.objectid[8],subhdr.objectid[9],
	    subhdr.objectid[10],subhdr.objectid[11],
	    subhdr.objectid[12],subhdr.objectid[13],
	    subhdr.objectid[14],subhdr.objectid[15]);

	}
	offset += subhdr.size;
    }


    DPRINTF(E_DBG,L_SCAN,"Successfully parsed file\n");
    r_close(wma_fd);
}
