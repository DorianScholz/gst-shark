/* GstShark - A Front End for GstTracer
 * Copyright (C) 2016 RidgeRun Engineering <manuel.leiva@ridgerun.com>
 *                                         <sebastian.fatjo@ridgerun.com>
 *
 * This file is part of GstShark.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gprintf.h>
#include <string.h>
#include <unistd.h>

#include "gstctf.h"

typedef enum
{
  BYTE_ORDER_BE,
  BYTE_ORDER_LE,
} byte_order;

struct _GstCtfDescriptor
{
  FILE *metadata;
  FILE *datastream;
  GMutex mutex;
  GstClockTime start_time;
};

static GstCtfDescriptor *ctf_descriptor = NULL;

/* Metadata format string */
static const char metadata_fmt[] = "\
/* CTF 1.8 */\n\
typealias integer { size = 8; align = 8; signed = false; } := uint8_t;\n\
typealias integer { size = 16; align = 8; signed = false; } := uint16_t;\n\
typealias integer { size = 32; align = 8; signed = false; } := uint32_t;\n\
typealias integer { size = 64; align = 8; signed = false; } := uint64_t;\n\
typealias integer { size = 5; align = 1; signed = false; } := uint5_t;\n\
typealias integer { size = 27; align = 1; signed = false; } := uint27_t;\n\
\n\
trace {\n\
	major = %u;\n\
	minor = %u;\n\
	uuid = \"%s\";\n\
	byte_order = %s;\n\
	packet.header := struct {\n\
		uint32_t magic;\n\
		uint8_t  uuid[16];\n\
		uint32_t stream_id;\n\
	};\n\
};\n\
\n\
clock { \n\
	name = monotonic; \n\
	uuid = \"84db105b-b3f4-4821-b662-efc51455106a\"; \n\
	description = \"Monotonic Clock\"; \n\
	freq = 1000000; /* Frequency, in Hz */ \n\
	/* clock value offset from Epoch is: offset * (1/freq) */ \n\
	/*offset = 2160000000;*/\n\
    offset_s = 21600; \n\
};\n\
\n\
typealias integer {\n\
	size = 32; align = 8; signed = false;\n\
	map = clock.monotonic.value;\n\
} := uint32_clock_monotonic_t;\n\
\n\
typealias integer {\n\
	size = 64; align = 8; signed = false;\n\
	map = clock.monotonic.value;\n\
} := uint64_clock_monotonic_t;\n\
\n\
struct packet_context {\n\
	uint64_clock_monotonic_t timestamp_begin;\n\
	uint64_clock_monotonic_t timestamp_end;\n\
	uint32_t events_discarded;\n\
	uint32_t cpu_id;\n\
};\n\
\n\
struct event_header_compact {\n\
	enum : uint5_t { compact = 0 ... 30, extended = 31 } id;\n\
	variant <id> {\n\
		struct {\n\
			uint27_t timestamp;\n\
		} compact;\n\
		struct {\n\
			uint32_t id;\n\
			uint64_t timestamp;\n\
		} extended;\n\
	} v;\n\
} align(8);\n\
\n\
struct event_header_large {\n\
	enum : uint16_t { compact = 0 ... 65534, extended = 65535 } id;\n\
	variant <id> {\n\
		struct {\n\
			uint32_t timestamp;\n\
		} compact;\n\
		struct {\n\
			uint32_t id;\n\
			uint64_t timestamp;\n\
		} extended;\n\
	} v;\n\
} align(8);\n\
\n\
stream {\n\
	id = 0;\n\
	event.header := struct event_header_large;\n\
	packet.context := struct packet_context;\n\
};\n\
\n\
event {\n\
	name = init;\n\
	id = 0;\n\
	stream_id = 0;\n\
};\n\
";

static void
generate_datastream_header (gchar * UUID, gint UUID_size, guint32 stream_id)
{
  guint64 time_stamp_begin;
  guint64 time_stamp_end;
  guint32 events_discarted;
  guint32 cpu_id;
  guint32 Magic = 0xC1FC1FC1;
  guint32 unknown;

  /* The begin of the data stream header is compound by the Magic Number,
     the trace UUID and the Stream ID. These are all required fields. */

  g_mutex_lock (&ctf_descriptor->mutex);
  /* Magic Number */
  fwrite (&Magic, sizeof (gchar), sizeof (guint32), ctf_descriptor->datastream);

  /* Trace UUID */
  fwrite (UUID, sizeof (gchar), UUID_size, ctf_descriptor->datastream);

  /* Stream ID */
  fwrite (&stream_id, sizeof (gchar), sizeof (guint32),
      ctf_descriptor->datastream);

  /* The following bytes correspond to the event packet context, these 
     fields are optional. */

  /* Time Stamp begin */
  time_stamp_begin = 0x3e3db41faf8;     // 0xf8fa41dbe3030000
  fwrite (&time_stamp_begin, sizeof (gchar), sizeof (guint64),
      ctf_descriptor->datastream);

  /* Time Stamp end */
  time_stamp_end = 0x000003e3ec8152ee;  // 0xee5281ece3030000;
  fwrite (&time_stamp_end, sizeof (gchar), sizeof (guint64),
      ctf_descriptor->datastream);

  /* Events discarted */
  events_discarted = 0x0;
  fwrite (&events_discarted, sizeof (gchar), sizeof (guint32),
      ctf_descriptor->datastream);

  /* CPU ID */
  cpu_id = 0x0;
  fwrite (&cpu_id, sizeof (gchar), sizeof (guint32),
      ctf_descriptor->datastream);

  /* Padding needed */
  unknown = 0x0000FFFF;
  fwrite (&unknown, sizeof (gchar), sizeof (guint32),
      ctf_descriptor->datastream);

  g_mutex_unlock (&ctf_descriptor->mutex);
}

static void
uuid_to_uuidstring (gchar * uuid_string, gchar * uuid)
{
  gchar *uuid_string_idx;
  gint32 byte;
  gint uuid_idx;

  uuid_string_idx = uuid_string;
  uuid_idx = 0;
  for (uuid_idx = 0; uuid_idx < 4; ++uuid_idx) {
    byte = 0xFF & uuid[uuid_idx];

    g_sprintf (uuid_string_idx, "%x", byte);
    uuid_string_idx += 2;
  }

  *(uuid_string_idx++) = '-';

  for (; uuid_idx < 6; ++uuid_idx) {
    byte = 0xFF & uuid[uuid_idx];
    g_sprintf (uuid_string_idx, "%x", byte);
    uuid_string_idx += 2;
  }
  *(uuid_string_idx++) = '-';

  for (; uuid_idx < 8; ++uuid_idx) {
    byte = 0xFF & uuid[uuid_idx];

    g_sprintf (uuid_string_idx, "%x", byte);
    uuid_string_idx += 2;
  }
  *(uuid_string_idx++) = '-';

  for (; uuid_idx < 10; ++uuid_idx) {
    byte = 0xFF & uuid[uuid_idx];
    g_sprintf (uuid_string_idx, "%x", byte);
    uuid_string_idx += 2;
  }
  *(uuid_string_idx++) = '-';

  for (; uuid_idx < 16; ++uuid_idx) {
    byte = 0xFF & uuid[uuid_idx];
    g_sprintf (uuid_string_idx, "%x", byte);
    uuid_string_idx += 2;
  }

  *++uuid_string_idx = 0;
}

static void
generate_metadata (gint major, gint minor, gchar * UUID, gint byte_order)
{
  /* Writing the first sections of the metadata file with the structures 
     and the definitions that will be needed in the future. */

  gchar uuid_string[] = "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX0";
  uuid_to_uuidstring (uuid_string, UUID);
  g_printf ("%s\n", uuid_string);

  g_mutex_lock (&ctf_descriptor->mutex);
  g_fprintf (ctf_descriptor->metadata, metadata_fmt, major, minor, uuid_string,
      byte_order ? "le" : "be");
  g_mutex_unlock (&ctf_descriptor->mutex);
}

static GstCtfDescriptor *
create_new_ctf (void)
{
  GstCtfDescriptor *ctf;
  const gchar *dir_name;
  //~ gchar *metadata_file;
  //~ gchar *datastream_file;
  //~ time_t now = time (NULL);

  //~ g_sprintf (metadata_file, "metadata");
  //~ g_sprintf (datastream_file, "datastream");
  /* Creating the output folder for the CTF output files. */
#if 0
  g_date_strftime (dir_name, 30, "gstshark_ctf_%Y%m%d%H%M%S", localtime (&now));
#else
  //g_sprintf (dir_name, "gstshark_ctf");
  dir_name = "gstshark_ctf";
#endif

  if (!g_file_test (dir_name, G_FILE_TEST_EXISTS)) {
    GST_ERROR ("@SFC: Creating %s directory.", dir_name);
    g_mkdir (dir_name, 0666);
  } else {
    GST_ERROR ("@SFC: Directory %s already exists.", dir_name);
  }

  /* Allocating memory space for the private structure that will 
     contains the file descriptors for the CTF ouput. */
  ctf = g_malloc (sizeof (GstCtfDescriptor));

  ctf->datastream = g_fopen ("datastream", "w");
  ctf->metadata = g_fopen ("metadata", "w");
  g_mutex_init (&ctf->mutex);
  ctf->start_time = gst_util_get_timestamp ();
  //g_free (dir_name);
  //~ g_free (datastream_file);
  //~ g_free (metadata_file);

  return ctf;
}

gboolean
gst_ctf_init (void)
{
  gchar UUID[] =
      { 0xd1, 0x8e, 0x63, 0x74, 0x35, 0xa1, 0xcd, 0x42, 0x8e, 0x70, 0xa9, 0xcf,
    0xfa, 0x71, 0x27, 0x93
  };

  if (ctf_descriptor) {
    GST_ERROR ("@SFC: Error! Structure already exits!");
    return FALSE;
  }

  ctf_descriptor = create_new_ctf ();
  generate_datastream_header (UUID, sizeof (UUID), 0);
  generate_metadata (1, 3, UUID, BYTE_ORDER_LE);

  do_print_ctf_init (INIT_EVENT_ID);


  return TRUE;
}

void
gst_ctf_close (void)
{
  fclose (ctf_descriptor->metadata);
  fclose (ctf_descriptor->datastream);
  g_mutex_clear (&ctf_descriptor->mutex);
  g_free (ctf_descriptor);
}

void
add_metadata_event_struct (const gchar * metadata_event)
{
  /* This function only writes the event structure to the metadata file, it
     depends entirely of what is passed as an argument. */
  g_mutex_lock (&ctf_descriptor->mutex);
  g_fprintf (ctf_descriptor->metadata, "%s", metadata_event);
  g_mutex_unlock (&ctf_descriptor->mutex);
}

static void
add_event_header (event_id id)
{
  guint32 timestamp;
  GstClockTime elapsed =
      GST_CLOCK_DIFF (ctf_descriptor->start_time, gst_util_get_timestamp ());
  elapsed = elapsed / 1000;
  timestamp = elapsed;
  /* Add event ID */
  fwrite (&id, sizeof (gchar), sizeof (gint16), ctf_descriptor->datastream);
  fwrite (&timestamp, sizeof (gchar), sizeof (guint32),
      ctf_descriptor->datastream);
}

void
do_print_cpuusage_event (event_id id, guint32 cpunum, guint64 cpuload)
{
  g_mutex_lock (&ctf_descriptor->mutex);
  add_event_header (id);
  fwrite (&cpunum, sizeof (gchar), sizeof (guint32),
      ctf_descriptor->datastream);
  fwrite (&cpuload, sizeof (gchar), sizeof (guint64),
      ctf_descriptor->datastream);
  g_mutex_unlock (&ctf_descriptor->mutex);
}


void
do_print_proctime_event (event_id id, gchar * elementname, guint64 time)
{
  gint size = strlen (elementname);


  g_mutex_lock (&ctf_descriptor->mutex);
  add_event_header (id);
  fwrite (elementname, sizeof (gchar), size + 1, ctf_descriptor->datastream);
  fwrite (&time, sizeof (gchar), sizeof (guint64), ctf_descriptor->datastream);
  g_mutex_unlock (&ctf_descriptor->mutex);
}

void
do_print_framerate_event (event_id id, const gchar * padname, guint64 fps)
{
  gint size = strlen (padname);

  g_mutex_lock (&ctf_descriptor->mutex);
  add_event_header (id);
  fwrite (padname, sizeof (gchar), size + 1, ctf_descriptor->datastream);

  fwrite (&fps, sizeof (gchar), sizeof (guint64), ctf_descriptor->datastream);
  g_mutex_unlock (&ctf_descriptor->mutex);
}

void
do_print_interlatency_event (event_id id,
    gchar * originpad, gchar * destinationpad, guint64 time)
{
  gint size = strlen (originpad);

  g_mutex_lock (&ctf_descriptor->mutex);
  add_event_header (id);
  fwrite (originpad, sizeof (gchar), size + 1, ctf_descriptor->datastream);

  size = strlen (destinationpad);
  fwrite (destinationpad, sizeof (gchar), size + 1, ctf_descriptor->datastream);

  fwrite (&time, sizeof (gchar), sizeof (guint64), ctf_descriptor->datastream);
  g_mutex_unlock (&ctf_descriptor->mutex);
}

void
do_print_scheduling_event (event_id id, gchar * elementname, guint64 time)
{
  gint size = strlen (elementname);

  g_mutex_lock (&ctf_descriptor->mutex);
  add_event_header (id);
  fwrite (elementname, sizeof (gchar), size + 1, ctf_descriptor->datastream);

  fwrite (&time, sizeof (gchar), sizeof (guint64), ctf_descriptor->datastream);
  g_mutex_unlock (&ctf_descriptor->mutex);
}

void
do_print_ctf_init (event_id id)
{
  guint32 unknown = 0;

  g_mutex_lock (&ctf_descriptor->mutex);
  add_event_header (id);
  fwrite (&unknown, sizeof (gchar), sizeof (guint32),
      ctf_descriptor->datastream);
  g_mutex_unlock (&ctf_descriptor->mutex);
}
