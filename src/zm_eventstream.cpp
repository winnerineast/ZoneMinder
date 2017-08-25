//
// ZoneMinder Event Class Implementation, $Date$, $Revision$
// Copyright (C) 2001-2008 Philip Coombes
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//

#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <glob.h>

#include "zm.h"
#include "zm_db.h"
#include "zm_time.h"
#include "zm_mpeg.h"
#include "zm_signal.h"
#include "zm_event.h"
#include "zm_monitor.h"

// sendfile tricks
extern "C"
{
#include "zm_sendfile.h"
}

bool EventStream::loadInitialEventData( int monitor_id, time_t event_time ) {
  static char sql[ZM_SQL_SML_BUFSIZ];

  snprintf( sql, sizeof(sql), "select Id from Events where MonitorId = %d and unix_timestamp( EndTime ) > %ld order by Id asc limit 1", monitor_id, event_time );

  if ( mysql_query( &dbconn, sql ) ) {
    Error( "Can't run query: %s", mysql_error( &dbconn ) );
    exit( mysql_errno( &dbconn ) );
  }

  MYSQL_RES *result = mysql_store_result( &dbconn );
  if ( !result ) {
    Error( "Can't use query result: %s", mysql_error( &dbconn ) );
    exit( mysql_errno( &dbconn ) );
  }
  MYSQL_ROW dbrow = mysql_fetch_row( result );

  if ( mysql_errno( &dbconn ) ) {
    Error( "Can't fetch row: %s", mysql_error( &dbconn ) );
    exit( mysql_errno( &dbconn ) );
  }

  int init_event_id = atoi( dbrow[0] );

  mysql_free_result( result );

  loadEventData( init_event_id );

  if ( event_time ) {
    curr_stream_time = event_time;
    curr_frame_id = 1;
    if ( event_time >= event_data->start_time ) {
      for (unsigned int i = 0; i < event_data->frame_count; i++ ) {
        //Info( "eft %d > et %d", event_data->frames[i].timestamp, event_time );
        if ( event_data->frames[i].timestamp >= event_time ) {
          curr_frame_id = i+1;
          Debug( 3, "Set cst:%.2f", curr_stream_time );
          Debug( 3, "Set cfid:%d", curr_frame_id );
          break;
        }
      }
      Debug( 3, "Skipping %ld frames", event_data->frame_count );
    }
  }
  return( true );
}

bool EventStream::loadInitialEventData( int init_event_id, unsigned int init_frame_id ) {
  loadEventData( init_event_id );

  if ( init_frame_id ) {
    curr_stream_time = event_data->frames[init_frame_id-1].timestamp;
    curr_frame_id = init_frame_id;
  } else {
    curr_stream_time = event_data->start_time;
  }

  return( true );
}

bool EventStream::loadEventData( int event_id ) {
  static char sql[ZM_SQL_MED_BUFSIZ];

  snprintf( sql, sizeof(sql), "SELECT MonitorId, Frames, unix_timestamp( StartTime ) AS StartTimestamp, (SELECT max(Delta)-min(Delta) FROM Frames WHERE EventId=Events.Id) AS Duration, DefaultVideo FROM Events WHERE Id = %d", event_id );

  if ( mysql_query( &dbconn, sql ) ) {
    Error( "Can't run query: %s", mysql_error( &dbconn ) );
    exit( mysql_errno( &dbconn ) );
  }

  MYSQL_RES *result = mysql_store_result( &dbconn );
  if ( !result ) {
    Error( "Can't use query result: %s", mysql_error( &dbconn ) );
    exit( mysql_errno( &dbconn ) );
  }

  if ( !mysql_num_rows( result ) ) {
    Fatal( "Unable to load event %d, not found in DB", event_id );
  }

  MYSQL_ROW dbrow = mysql_fetch_row( result );

  if ( mysql_errno( &dbconn ) ) {
    Error( "Can't fetch row: %s", mysql_error( &dbconn ) );
    exit( mysql_errno( &dbconn ) );
  }

  delete event_data;
  event_data = new EventData;
  event_data->event_id = event_id;
  event_data->monitor_id = atoi( dbrow[0] );
  event_data->start_time = atoi(dbrow[2]);
  if ( config.use_deep_storage ) {
    struct tm *event_time = localtime( &event_data->start_time );
    if ( staticConfig.DIR_EVENTS.c_str()[0] == '/' )
      snprintf( event_data->path, sizeof(event_data->path), "%s/%ld/%02d/%02d/%02d/%02d/%02d/%02d", staticConfig.DIR_EVENTS.c_str(), event_data->monitor_id, event_time->tm_year-100, event_time->tm_mon+1, event_time->tm_mday, event_time->tm_hour, event_time->tm_min, event_time->tm_sec );
    else
      snprintf( event_data->path, sizeof(event_data->path), "%s/%s/%ld/%02d/%02d/%02d/%02d/%02d/%02d", staticConfig.PATH_WEB.c_str(), staticConfig.DIR_EVENTS.c_str(), event_data->monitor_id, event_time->tm_year-100, event_time->tm_mon+1, event_time->tm_mday, event_time->tm_hour, event_time->tm_min, event_time->tm_sec );
  } else {
    if ( staticConfig.DIR_EVENTS.c_str()[0] == '/' )
      snprintf( event_data->path, sizeof(event_data->path), "%s/%ld/%ld", staticConfig.DIR_EVENTS.c_str(), event_data->monitor_id, event_data->event_id );
    else
      snprintf( event_data->path, sizeof(event_data->path), "%s/%s/%ld/%ld", staticConfig.PATH_WEB.c_str(), staticConfig.DIR_EVENTS.c_str(), event_data->monitor_id, event_data->event_id );
  }
  event_data->frame_count = dbrow[1] == NULL ? 0 : atoi(dbrow[1]);
  event_data->duration = atof(dbrow[3]);
  strncpy( event_data->video_file, dbrow[4], sizeof( event_data->video_file )-1 );

  updateFrameRate( (double)event_data->frame_count/event_data->duration );

  mysql_free_result( result );

  snprintf( sql, sizeof(sql), "select FrameId, unix_timestamp( `TimeStamp` ), Delta from Frames where EventId = %d order by FrameId asc", event_id );
  if ( mysql_query( &dbconn, sql ) ) {
    Error( "Can't run query: %s", mysql_error( &dbconn ) );
    exit( mysql_errno( &dbconn ) );
  }

  result = mysql_store_result( &dbconn );
  if ( !result ) {
    Error( "Can't use query result: %s", mysql_error( &dbconn ) );
    exit( mysql_errno( &dbconn ) );
  }

  event_data->n_frames = mysql_num_rows( result );

  event_data->frames = new FrameData[event_data->frame_count];
  int id, last_id = 0;
  time_t timestamp, last_timestamp = event_data->start_time;
  double delta, last_delta = 0.0;
  while ( ( dbrow = mysql_fetch_row( result ) ) ) {
    id = atoi(dbrow[0]);
    timestamp = atoi(dbrow[1]);
    delta = atof(dbrow[2]);
    int id_diff = id - last_id;
    double frame_delta = (delta-last_delta)/id_diff;
    if ( id_diff > 1 ) {
      for ( int i = last_id+1; i < id; i++ ) {
        event_data->frames[i-1].timestamp = (time_t)(last_timestamp + ((i-last_id)*frame_delta));
        event_data->frames[i-1].offset = (time_t)(event_data->frames[i-1].timestamp-event_data->start_time);
        event_data->frames[i-1].delta = frame_delta;
        event_data->frames[i-1].in_db = false;
      }
    }
    event_data->frames[id-1].timestamp = timestamp;
    event_data->frames[id-1].offset = (time_t)(event_data->frames[id-1].timestamp-event_data->start_time);
    event_data->frames[id-1].delta = id>1?frame_delta:0.0;
    event_data->frames[id-1].in_db = true;
    last_id = id;
    last_delta = delta;
    last_timestamp = timestamp;
  }
  if ( mysql_errno( &dbconn ) ) {
    Error( "Can't fetch row: %s", mysql_error( &dbconn ) );
    exit( mysql_errno( &dbconn ) );
  }

  //for ( int i = 0; i < 250; i++ )
  //{
    //Info( "%d -> %d @ %f (%d)", i+1, event_data->frames[i].timestamp, event_data->frames[i].delta, event_data->frames[i].in_db );
  //}

  mysql_free_result( result );

  if ( forceEventChange || mode == MODE_ALL_GAPLESS ) {
    if ( replay_rate > 0 )
      curr_stream_time = event_data->frames[0].timestamp;
    else
      curr_stream_time = event_data->frames[event_data->frame_count-1].timestamp;
  }
  Debug( 2, "Event:%ld, Frames:%ld, Duration: %.2f", event_data->event_id, event_data->frame_count, event_data->duration );

  return( true );
}

void EventStream::processCommand( const CmdMsg *msg ) {
  Debug( 2, "Got message, type %d, msg %d", msg->msg_type, msg->msg_data[0] );
  // Check for incoming command
  switch( (MsgCommand)msg->msg_data[0] ) {
    case CMD_PAUSE :
      {
        Debug( 1, "Got PAUSE command" );

        // Set paused flag
        paused = true;
        replay_rate = ZM_RATE_BASE;
        last_frame_sent = TV_2_FLOAT( now );
        break;
      }
    case CMD_PLAY :
      {
        Debug( 1, "Got PLAY command" );
        if ( paused )
        {
          // Clear paused flag
          paused = false;
        }

        // If we are in single event mode and at the last frame, replay the current event
        if ( (mode == MODE_SINGLE) && ((unsigned int)curr_frame_id == event_data->frame_count) ) {
          curr_frame_id = 1;
          Debug(1, "Was in single_mode, and last frame, so jumping to 1st frame");
          curr_frame_id = 1;
        } else {
          Debug(1, "mode is %s, current frame is %d, frame count is %d", (mode == MODE_SINGLE ? "single" : "not single" ), curr_frame_id, event_data->frame_count );
        }

        replay_rate = ZM_RATE_BASE;
        break;
      }
    case CMD_VARPLAY :
      {
        Debug( 1, "Got VARPLAY command" );
        if ( paused )
        {
          // Clear paused flag
          paused = false;
        }
        replay_rate = ntohs(((unsigned char)msg->msg_data[2]<<8)|(unsigned char)msg->msg_data[1])-32768;
        break;
      }
    case CMD_STOP :
      {
        Debug( 1, "Got STOP command" );

        // Clear paused flag
        paused = false;
        break;
      }
    case CMD_FASTFWD :
      {
        Debug( 1, "Got FAST FWD command" );
        if ( paused ) {
          // Clear paused flag
          paused = false;
        }
        // Set play rate
        switch ( replay_rate ) {
          case 2 * ZM_RATE_BASE :
            replay_rate = 5 * ZM_RATE_BASE;
            break;
          case 5 * ZM_RATE_BASE :
            replay_rate = 10 * ZM_RATE_BASE;
            break;
          case 10 * ZM_RATE_BASE :
            replay_rate = 25 * ZM_RATE_BASE;
            break;
          case 25 * ZM_RATE_BASE :
          case 50 * ZM_RATE_BASE :
            replay_rate = 50 * ZM_RATE_BASE;
            break;
          default :
            replay_rate = 2 * ZM_RATE_BASE;
            break;
        }
        break;
      }
    case CMD_SLOWFWD :
      {
        Debug( 1, "Got SLOW FWD command" );
        // Set paused flag
        paused = true;
        // Set play rate
        replay_rate = ZM_RATE_BASE;
        // Set step
        step = 1;
        break;
      }
    case CMD_SLOWREV :
      {
        Debug( 1, "Got SLOW REV command" );
        // Set paused flag
        paused = true;
        // Set play rate
        replay_rate = ZM_RATE_BASE;
        // Set step
        step = -1;
        break;
      }
    case CMD_FASTREV :
      {
        Debug( 1, "Got FAST REV command" );
        if ( paused ) {
          // Clear paused flag
          paused = false;
        }
        // Set play rate
        switch ( replay_rate ) {
          case -2 * ZM_RATE_BASE :
            replay_rate = -5 * ZM_RATE_BASE;
            break;
          case -5 * ZM_RATE_BASE :
            replay_rate = -10 * ZM_RATE_BASE;
            break;
          case -10 * ZM_RATE_BASE :
            replay_rate = -25 * ZM_RATE_BASE;
            break;
          case -25 * ZM_RATE_BASE :
          case -50 * ZM_RATE_BASE :
            replay_rate = -50 * ZM_RATE_BASE;
            break;
          default :
            replay_rate = -2 * ZM_RATE_BASE;
            break;
        }
        break;
      }
    case CMD_ZOOMIN :
      {
        x = ((unsigned char)msg->msg_data[1]<<8)|(unsigned char)msg->msg_data[2];
        y = ((unsigned char)msg->msg_data[3]<<8)|(unsigned char)msg->msg_data[4];
        Debug( 1, "Got ZOOM IN command, to %d,%d", x, y );
        switch ( zoom ) {
          case 100:
            zoom = 150;
            break;
          case 150:
            zoom = 200;
            break;
          case 200:
            zoom = 300;
            break;
          case 300:
            zoom = 400;
            break;
          case 400:
          default :
            zoom = 500;
            break;
        }
        send_frame = true;
        break;
      }
    case CMD_ZOOMOUT :
      {
        Debug( 1, "Got ZOOM OUT command" );
        switch ( zoom ) {
          case 500:
            zoom = 400;
            break;
          case 400:
            zoom = 300;
            break;
          case 300:
            zoom = 200;
            break;
          case 200:
            zoom = 150;
            break;
          case 150:
          default :
            zoom = 100;
            break;
        }
        break;
        send_frame = true;
      }
    case CMD_PAN :
      {
        x = ((unsigned char)msg->msg_data[1]<<8)|(unsigned char)msg->msg_data[2];
        y = ((unsigned char)msg->msg_data[3]<<8)|(unsigned char)msg->msg_data[4];
        Debug( 1, "Got PAN command, to %d,%d", x, y );
        break;
      }
    case CMD_SCALE :
      {
        scale = ((unsigned char)msg->msg_data[1]<<8)|(unsigned char)msg->msg_data[2];
        Debug( 1, "Got SCALE command, to %d", scale );
        break;
      }
    case CMD_PREV :
      {
        Debug( 1, "Got PREV command" );
        if ( replay_rate >= 0 )
          curr_frame_id = 0;
        else
          curr_frame_id = event_data->frame_count+1;
        paused = false;
        forceEventChange = true;
        break;
      }
    case CMD_NEXT :
      {
        Debug( 1, "Got NEXT command" );
        if ( replay_rate >= 0 )
          curr_frame_id = event_data->frame_count+1;
        else
          curr_frame_id = 0;
        paused = false;
        forceEventChange = true;
        break;
      }
    case CMD_SEEK :
      {
        int offset = ((unsigned char)msg->msg_data[1]<<24)|((unsigned char)msg->msg_data[2]<<16)|((unsigned char)msg->msg_data[3]<<8)|(unsigned char)msg->msg_data[4];
        curr_frame_id = (int)(event_data->frame_count*offset/event_data->duration);
        Debug( 1, "Got SEEK command, to %d (new cfid: %d)", offset, curr_frame_id );
        send_frame = true;
        break;
      }
    case CMD_QUERY :
      {
        Debug( 1, "Got QUERY command, sending STATUS" );
        break;
      }
    case CMD_QUIT :
      {
        Info ("User initiated exit - CMD_QUIT");
        break;
      }
    default :
      {
        // Do nothing, for now
      }
  }
  struct {
    int event;
    int progress;
    int rate;
    int zoom;
    bool paused;
  } status_data;

  status_data.event = event_data->event_id;
  status_data.progress = (int)event_data->frames[curr_frame_id-1].offset;
  status_data.rate = replay_rate;
  status_data.zoom = zoom;
  status_data.paused = paused;
  Debug( 2, "Event:%d, Paused:%d, progress:%d Rate:%d, Zoom:%d",
    status_data.event,
    status_data.paused,
    status_data.progress,
    status_data.rate,
    status_data.zoom
  );

  DataMsg status_msg;
  status_msg.msg_type = MSG_DATA_EVENT;
  memcpy( &status_msg.msg_data, &status_data, sizeof(status_data) );
  if ( sendto( sd, &status_msg, sizeof(status_msg), MSG_DONTWAIT, (sockaddr *)&rem_addr, sizeof(rem_addr) ) < 0 ) {
    //if ( errno != EAGAIN )
    {
      Error( "Can't sendto on sd %d: %s", sd, strerror(errno) );
      exit( -1 );
    }
  }
  // quit after sending a status, if this was a quit request
  if ((MsgCommand)msg->msg_data[0]==CMD_QUIT)
    exit(0);

  updateFrameRate( (double)event_data->frame_count/event_data->duration );
}

void EventStream::checkEventLoaded() {
  bool reload_event = false;
  static char sql[ZM_SQL_SML_BUFSIZ];

  if ( curr_frame_id <= 0 ) {
    snprintf( sql, sizeof(sql), "select Id from Events where MonitorId = %ld and Id < %ld order by Id desc limit 1", event_data->monitor_id, event_data->event_id );
    reload_event = true;
  } else if ( (unsigned int)curr_frame_id > event_data->frame_count ) {
    snprintf( sql, sizeof(sql), "select Id from Events where MonitorId = %ld and Id > %ld order by Id asc limit 1", event_data->monitor_id, event_data->event_id );
    reload_event = true;
  }

  if ( reload_event ) {
    if ( forceEventChange || mode != MODE_SINGLE ) {
      //Info( "SQL:%s", sql );
      if ( mysql_query( &dbconn, sql ) ) {
        Error( "Can't run query: %s", mysql_error( &dbconn ) );
        exit( mysql_errno( &dbconn ) );
      }

      MYSQL_RES *result = mysql_store_result( &dbconn );
      if ( !result ) {
        Error( "Can't use query result: %s", mysql_error( &dbconn ) );
        exit( mysql_errno( &dbconn ) );
      }
      MYSQL_ROW dbrow = mysql_fetch_row( result );

      if ( mysql_errno( &dbconn ) ) {
        Error( "Can't fetch row: %s", mysql_error( &dbconn ) );
        exit( mysql_errno( &dbconn ) );
      }

      if ( dbrow ) {
        int event_id = atoi(dbrow[0]);
        Debug( 1, "Loading new event %d", event_id );

        loadEventData( event_id );

        Debug( 2, "Current frame id = %d", curr_frame_id );
        if ( replay_rate < 0 )
          curr_frame_id = event_data->frame_count;
        else
          curr_frame_id = 1;
        Debug( 2, "New frame id = %d", curr_frame_id );
      } else {
        if ( curr_frame_id <= 0 )
          curr_frame_id = 1;
        else
          curr_frame_id = event_data->frame_count;
        paused = true;
      }
      mysql_free_result( result );
      forceEventChange = false;
    } else {
      if ( curr_frame_id <= 0 )
        curr_frame_id = 1;
      else
        curr_frame_id = event_data->frame_count;
      paused = true;
    }
  }
}

bool EventStream::sendFrame( int delta_us ) {
  Debug( 2, "Sending frame %d", curr_frame_id );

  static char filepath[PATH_MAX];
  static struct stat filestat;
  FILE *fdj = NULL;

  // This needs to be abstracted.  If we are saving jpgs, then load the capture file.  If we are only saving analysis frames, then send that.
  if ( monitor->GetOptSaveJPEGs() & 1 ) {
    snprintf( filepath, sizeof(filepath), Event::capture_file_format, event_data->path, curr_frame_id );
  } else if ( monitor->GetOptSaveJPEGs() & 2 ) {
    snprintf( filepath, sizeof(filepath), Event::analyse_file_format, event_data->path, curr_frame_id );
    if ( stat( filepath, &filestat ) < 0 ) {
      Debug(1, "%s not found, dalling back to capture");
      snprintf( filepath, sizeof(filepath), Event::capture_file_format, event_data->path, curr_frame_id );
    }

  } else {
    Fatal("JPEGS not saved.zms is not capable of streaming jpegs from mp4 yet");
    return false;
  }

#if HAVE_LIBAVCODEC
  if ( type == STREAM_MPEG ) {
    Image image( filepath );

    Image *send_image = prepareImage( &image );

    if ( !vid_stream ) {
      vid_stream = new VideoStream( "pipe:", format, bitrate, effective_fps, send_image->Colours(), send_image->SubpixelOrder(), send_image->Width(), send_image->Height() );
      fprintf( stdout, "Content-type: %s\r\n\r\n", vid_stream->MimeType() );
      vid_stream->OpenStream();
    }
    /* double pts = */ vid_stream->EncodeFrame( send_image->Buffer(), send_image->Size(), config.mpeg_timed_frames, delta_us*1000 );
  } else
#endif // HAVE_LIBAVCODEC
  {
    static unsigned char temp_img_buffer[ZM_MAX_IMAGE_SIZE];

    int img_buffer_size = 0;
    uint8_t *img_buffer = temp_img_buffer;

    bool send_raw = ((scale>=ZM_SCALE_BASE)&&(zoom==ZM_SCALE_BASE));

    fprintf( stdout, "--ZoneMinderFrame\r\n" );

    if ( type != STREAM_JPEG )
      send_raw = false;

    if ( send_raw ) {
      fdj = fopen( filepath, "rb" );
      if ( !fdj ) {
        Error( "Can't open %s: %s", filepath, strerror(errno) );
        return( false );
      }
#if HAVE_SENDFILE
      if( fstat(fileno(fdj),&filestat) < 0 ) {
        Error( "Failed getting information about file %s: %s", filepath, strerror(errno) );
        return( false );
      }
#else
      img_buffer_size = fread( img_buffer, 1, sizeof(temp_img_buffer), fdj );
#endif
    } else {
      Image image( filepath );

      Image *send_image = prepareImage( &image );

      switch( type ) {
        case STREAM_JPEG :
          send_image->EncodeJpeg( img_buffer, &img_buffer_size );
          break;
        case STREAM_ZIP :
#if HAVE_ZLIB_H
          unsigned long zip_buffer_size;
          send_image->Zip( img_buffer, &zip_buffer_size );
          img_buffer_size = zip_buffer_size;
          break;
#else
          Error("zlib is required for zipped images. Falling back to raw image");
          type = STREAM_RAW;
#endif // HAVE_ZLIB_H
        case STREAM_RAW :
          img_buffer = (uint8_t*)(send_image->Buffer());
          img_buffer_size = send_image->Size();
          break;
        default:
          Fatal( "Unexpected frame type %d", type );
          break;
      }
    }

    switch( type ) {
      case STREAM_JPEG :
        fprintf( stdout, "Content-Type: image/jpeg\r\n" );
        break;
      case STREAM_RAW :
        fprintf( stdout, "Content-Type: image/x-rgb\r\n" );
        break;
      case STREAM_ZIP :
        fprintf( stdout, "Content-Type: image/x-rgbz\r\n" );
        break;
      default :
        Fatal( "Unexpected frame type %d", type );
        break;
    }


    if(send_raw) {
#if HAVE_SENDFILE
      fprintf( stdout, "Content-Length: %d\r\n\r\n", (int)filestat.st_size );
      if(zm_sendfile(fileno(stdout), fileno(fdj), 0, (int)filestat.st_size) != (int)filestat.st_size) {
        /* sendfile() failed, use standard way instead */
        img_buffer_size = fread( img_buffer, 1, sizeof(temp_img_buffer), fdj );
        if ( fwrite( img_buffer, img_buffer_size, 1, stdout ) != 1 ) {
          Error("Unable to send raw frame %u: %s",curr_frame_id,strerror(errno));
          return( false );
        }
      }
#else
      fprintf( stdout, "Content-Length: %d\r\n\r\n", img_buffer_size );
      if ( fwrite( img_buffer, img_buffer_size, 1, stdout ) != 1 ) {
        Error("Unable to send raw frame %u: %s",curr_frame_id,strerror(errno));
        return( false );
      }
#endif
      fclose(fdj); /* Close the file handle */
    } else {
      fprintf( stdout, "Content-Length: %d\r\n\r\n", img_buffer_size );
      if ( fwrite( img_buffer, img_buffer_size, 1, stdout ) != 1 ) {
        Error( "Unable to send stream frame: %s", strerror(errno) );
        return( false );
      }
    }

    fprintf( stdout, "\r\n\r\n" );
    fflush( stdout );
  }
  last_frame_sent = TV_2_FLOAT( now );
  return( true );
}

void EventStream::runStream() {
  Event::Initialise();

  openComms();

  checkInitialised();

  updateFrameRate( (double)event_data->frame_count/event_data->duration );

  if ( type == STREAM_JPEG )
    fprintf( stdout, "Content-Type: multipart/x-mixed-replace;boundary=ZoneMinderFrame\r\n\r\n" );

  if ( !event_data ) {
    sendTextFrame( "No event data found" );
    exit( 0 );
  }

  while( !zm_terminate ) {
    gettimeofday( &now, NULL );

    unsigned int delta_us = 0;
    send_frame = false;

    // commands may set send_frame to true
    while(checkCommandQueue());

    if ( step != 0 )
      curr_frame_id += step;

    checkEventLoaded();

    // Get current frame data
    FrameData *frame_data = &event_data->frames[curr_frame_id-1];

    //Info( "cst:%.2f", curr_stream_time );
    //Info( "cfid:%d", curr_frame_id );
    //Info( "fdt:%d", frame_data->timestamp );
    if ( !paused ) {
      bool in_event = true;
      double time_to_event = 0;
      if ( replay_rate > 0 ) {
        time_to_event = event_data->frames[0].timestamp - curr_stream_time;
        if ( time_to_event > 0 )
          in_event = false;
      } else if ( replay_rate < 0 ) {
        time_to_event = curr_stream_time - event_data->frames[event_data->frame_count-1].timestamp;
        if ( time_to_event > 0 )
          in_event = false;
      }
      if ( !in_event ) {
        double actual_delta_time = TV_2_FLOAT( now ) - last_frame_sent;
        if ( actual_delta_time > 1 ) {
          static char frame_text[64];
          snprintf( frame_text, sizeof(frame_text), "Time to next event = %d seconds", (int)time_to_event );
          if ( !sendTextFrame( frame_text ) )
            zm_terminate = true;
        }
        //else
        //{
          usleep( STREAM_PAUSE_WAIT );
          //curr_stream_time += (replay_rate>0?1:-1) * ((1.0L * replay_rate * STREAM_PAUSE_WAIT)/(ZM_RATE_BASE * 1000000));
          curr_stream_time += (1.0L * replay_rate * STREAM_PAUSE_WAIT)/(ZM_RATE_BASE * 1000000);
        //}
        continue;
      }

      // Figure out if we should send this frame

      // If we are streaming and this frame is due to be sent
      if ( ((curr_frame_id-1)%frame_mod) == 0 ) {
        delta_us = (unsigned int)(frame_data->delta * 1000000);
        // if effective > base we should speed up frame delivery
        delta_us = (unsigned int)((delta_us * base_fps)/effective_fps);
        // but must not exceed maxfps
        delta_us = max(delta_us, 1000000 / maxfps);
        send_frame = true;
      }
    } else if ( step != 0 ) {
      // We are paused and are just stepping forward or backward one frame
      step = 0;
      send_frame = true;
    } else if ( !send_frame ) {
      // We are paused, and doing nothing
      double actual_delta_time = TV_2_FLOAT( now ) - last_frame_sent;
      if ( actual_delta_time > MAX_STREAM_DELAY ) {
        // Send keepalive
        Debug( 2, "Sending keepalive frame" );
        send_frame = true;
      }
    }

    if ( send_frame )
      if ( !sendFrame( delta_us ) )
        zm_terminate = true;

    curr_stream_time = frame_data->timestamp;

    if ( !paused ) {
      curr_frame_id += replay_rate>0?1:-1;
      if ( send_frame && type != STREAM_MPEG ) {
        Debug( 3, "dUs: %d", delta_us );
        if ( delta_us )
          usleep( delta_us );
      }
    } else {
      usleep( (unsigned long)((1000000 * ZM_RATE_BASE)/((base_fps?base_fps:1)*abs(replay_rate*2))) );
    }
  } // end while ! zm_terminate
#if HAVE_LIBAVCODEC
  if ( type == STREAM_MPEG )
    delete vid_stream;
#endif // HAVE_LIBAVCODEC

  closeComms();
}
