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
#include "zm_signal.h"
#include "zm_event.h"
#include "zm_monitor.h"

//#define USE_PREPARED_SQL 1

bool Event::initialised = false;
char Event::capture_file_format[PATH_MAX];
char Event::analyse_file_format[PATH_MAX];
char Event::general_file_format[PATH_MAX];
char Event::video_file_format[PATH_MAX];

int Event::pre_alarm_count = 0;
Event::PreAlarmData Event::pre_alarm_data[MAX_PRE_ALARM_FRAMES] = { { 0 } };

Event::Event( Monitor *p_monitor, struct timeval p_start_time, const std::string &p_cause, const StringSetMap &p_noteSetMap, bool p_videoEvent ) :
  monitor( p_monitor ),
  start_time( p_start_time ),
  cause( p_cause ),
  noteSetMap( p_noteSetMap ),
  videoEvent( p_videoEvent ),
  videowriter( NULL )
{
  if ( !initialised )
    Initialise();

  std::string notes;
  createNotes( notes );

  bool untimedEvent = false;
  if ( !start_time.tv_sec ) {
    untimedEvent = true;
    gettimeofday( &start_time, 0 );
  }

  unsigned int state_id = 0;
  if ( MYSQL_ROW dbrow = zmDbFetchOne( "SELECT Id FROM States WHERE IsActive=1" ) ) {
    state_id = atoi(dbrow[0]);
  }

  static char sql[ZM_SQL_MED_BUFSIZ];
  struct tm *stime = localtime( &start_time.tv_sec );
  snprintf( sql, sizeof(sql), "insert into Events ( MonitorId, Name, StartTime, Width, Height, Cause, Notes, StateId, Videoed ) values ( %d, 'New Event', from_unixtime( %ld ), %d, %d, '%s', '%s', '%d', '%d' )", monitor->Id(), start_time.tv_sec, monitor->Width(), monitor->Height(), cause.c_str(), notes.c_str(), state_id, videoEvent );
  if ( mysql_query( &dbconn, sql ) ) {
    Error( "Can't insert event: %s. sql was (%s)", mysql_error( &dbconn ), sql );
    exit( mysql_errno( &dbconn ) );
  }
  id = mysql_insert_id( &dbconn );
  if ( untimedEvent ) {
    Warning( "Event %d has zero time, setting to current", id );
  }
  end_time.tv_sec = 0;
  frames = 0;
  alarm_frames = 0;
  tot_score = 0;
  max_score = 0;

  struct stat statbuf;
  char id_file[PATH_MAX];

  if ( config.use_deep_storage ) {
    char *path_ptr = path;
    path_ptr += snprintf( path_ptr, sizeof(path), "%s/%d", staticConfig.DIR_EVENTS.c_str(), monitor->Id() );

    int dt_parts[6];
    dt_parts[0] = stime->tm_year-100;
    dt_parts[1] = stime->tm_mon+1;
    dt_parts[2] = stime->tm_mday;
    dt_parts[3] = stime->tm_hour;
    dt_parts[4] = stime->tm_min;
    dt_parts[5] = stime->tm_sec;

    char date_path[PATH_MAX] = "";
    char time_path[PATH_MAX] = "";
    char *time_path_ptr = time_path;
    for ( unsigned int i = 0; i < sizeof(dt_parts)/sizeof(*dt_parts); i++ ) {
      path_ptr += snprintf( path_ptr, sizeof(path)-(path_ptr-path), "/%02d", dt_parts[i] );

      errno = 0;
      if ( stat( path, &statbuf ) ) {
        if ( errno == ENOENT || errno == ENOTDIR ) {
          if ( mkdir( path, 0755 ) ) {
            Fatal( "Can't mkdir %s: %s", path, strerror(errno));
          }
        } else {
          Warning( "Error stat'ing %s, may be fatal. error is %s", path, strerror(errno));
        }
      }
      if ( i == 2 )
        strncpy( date_path, path, sizeof(date_path) );
      else if ( i >= 3 )
        time_path_ptr += snprintf( time_path_ptr, sizeof(time_path)-(time_path_ptr-time_path), "%s%02d", i>3?"/":"", dt_parts[i] );
    }
    // Create event id symlink
    snprintf( id_file, sizeof(id_file), "%s/.%d", date_path, id );
    if ( symlink( time_path, id_file ) < 0 )
      Fatal( "Can't symlink %s -> %s: %s", id_file, path, strerror(errno));
  } else {
    snprintf( path, sizeof(path), "%s/%d/%d", staticConfig.DIR_EVENTS.c_str(), monitor->Id(), id );

    errno = 0;
    stat( path, &statbuf );
    if ( errno == ENOENT || errno == ENOTDIR ) {
      if ( mkdir( path, 0755 ) ) {
        Error( "Can't mkdir %s: %s", path, strerror(errno));
      }
    }
  } // deep storage or not

  // Create empty id tag file
  snprintf( id_file, sizeof(id_file), "%s/.%d", path, id );
  if ( FILE *id_fp = fopen( id_file, "w" ) )
    fclose( id_fp );
  else
    Fatal( "Can't fopen %s: %s", id_file, strerror(errno));

  last_db_frame = 0;

  video_name[0] = 0;

  /* Save as video */

  if ( monitor->GetOptVideoWriter() != 0 ) {
    snprintf( video_name, sizeof(video_name), "%d-%s", id, "video.mp4" );
    snprintf( video_file, sizeof(video_file), video_file_format, path, video_name );

    /* X264 MP4 video writer */
    if ( monitor->GetOptVideoWriter() == Monitor::X264ENCODE ) {
#if ZM_HAVE_VIDEOWRITER_X264MP4
      videowriter = new X264MP4Writer(video_file, monitor->Width(), monitor->Height(), monitor->Colours(), monitor->SubpixelOrder(), monitor->GetOptEncoderParams());
#else
      Error("ZoneMinder was not compiled with the X264 MP4 video writer, check dependencies (x264 and mp4v2)");
#endif
    }

    if ( videowriter != NULL ) {
      /* Open the video stream */
      int nRet = videowriter->Open();
      if(nRet != 0) {
        Error("Failed opening video stream");
        delete videowriter;
        videowriter = NULL;
      }

      snprintf( timecodes_name, sizeof(timecodes_name), "%d-%s", id, "video.timecodes" );
      snprintf( timecodes_file, sizeof(timecodes_file), video_file_format, path, timecodes_name );

      /* Create timecodes file */
      timecodes_fd = fopen(timecodes_file, "wb");
      if ( timecodes_fd == NULL ) {
        Error("Failed creating timecodes file");
      }
    }
  } else {
    /* No video object */
    videowriter = NULL;
  }

} // Event::Event( Monitor *p_monitor, struct timeval p_start_time, const std::string &p_cause, const StringSetMap &p_noteSetMap, bool p_videoEvent )

Event::~Event() {
  static char sql[ZM_SQL_MED_BUFSIZ];
  struct DeltaTimeval delta_time;
  DELTA_TIMEVAL( delta_time, end_time, start_time, DT_PREC_2 );

  if ( frames > last_db_frame ) {

    Debug( 1, "Adding closing frame %d to DB", frames );
    snprintf( sql, sizeof(sql), "insert into Frames ( EventId, FrameId, TimeStamp, Delta ) values ( %d, %d, from_unixtime( %ld ), %s%ld.%02ld )", id, frames, end_time.tv_sec, delta_time.positive?"":"-", delta_time.sec, delta_time.fsec );
    if ( mysql_query( &dbconn, sql ) ) {
      Error( "Can't insert frame: %s", mysql_error( &dbconn ) );
      exit( mysql_errno( &dbconn ) );
    }
  }

  /* Close the video file */
  if ( videowriter != NULL ) {
    int nRet = videowriter->Close();
    if(nRet != 0) {
      Error("Failed closing video stream");
    }
    delete videowriter;
    videowriter = NULL;

    /* Close the timecodes file */
    fclose(timecodes_fd);
    timecodes_fd = NULL;
  }

  snprintf( sql, sizeof(sql), "update Events set Name='%s%d', EndTime = from_unixtime( %ld ), Length = %s%ld.%02ld, Frames = %d, AlarmFrames = %d, TotScore = %d, AvgScore = %d, MaxScore = %d, DefaultVideo = '%s' where Id = %d", monitor->EventPrefix(), id, end_time.tv_sec, delta_time.positive?"":"-", delta_time.sec, delta_time.fsec, frames, alarm_frames, tot_score, (int)(alarm_frames?(tot_score/alarm_frames):0), max_score, video_name, id );
  if ( mysql_query( &dbconn, sql ) ) {
    Error( "Can't update event: %s", mysql_error( &dbconn ) );
    exit( mysql_errno( &dbconn ) );
  }
}

void Event::createNotes( std::string &notes ) {
  notes.clear();
  for ( StringSetMap::const_iterator mapIter = noteSetMap.begin(); mapIter != noteSetMap.end(); mapIter++ ) {
    notes += mapIter->first;
    notes += ": ";
    const StringSet &stringSet = mapIter->second;
    for ( StringSet::const_iterator setIter = stringSet.begin(); setIter != stringSet.end(); setIter++ ) {
      if ( setIter != stringSet.begin() )
        notes += ", ";
      notes += *setIter;
    }
  }
}

int Event::sd = -1;

bool Event::WriteFrameImage( Image *image, struct timeval timestamp, const char *event_file, bool alarm_frame ) {
  Image* ImgToWrite;
  Image* ts_image = NULL;

  if ( !config.timestamp_on_capture )  // stash the image we plan to use in another pointer regardless if timestamped.
  {
    ts_image = new Image(*image);
    monitor->TimestampImage( ts_image, &timestamp );
    ImgToWrite=ts_image;
  } else
    ImgToWrite=image;

  int thisquality = ( alarm_frame && (config.jpeg_alarm_file_quality > config.jpeg_file_quality) ) ? config.jpeg_alarm_file_quality : 0 ;   // quality to use, zero is default
  ImgToWrite->WriteJpeg( event_file, thisquality, (monitor->Exif() ? timestamp : (timeval){0,0}) ); // exif is only timestamp at present this switches on or off for write

  if(ts_image) delete(ts_image); // clean up if used.
  return( true );
}

bool Event::WriteFrameVideo( const Image *image, const struct timeval timestamp, VideoWriter* videow ) {
  const Image* frameimg = image;
  Image ts_image;

  /* Checking for invalid parameters */
  if ( videow == NULL ) {
    Error("NULL Video object");
    return false;
  }

  /* If the image does not contain a timestamp, add the timestamp */
  if (!config.timestamp_on_capture) {
    ts_image = *image;
    monitor->TimestampImage( &ts_image, &timestamp );
    frameimg = &ts_image;
  }

  /* Calculate delta time */
  struct DeltaTimeval delta_time3;
  DELTA_TIMEVAL( delta_time3, timestamp, start_time, DT_PREC_3 );
  unsigned int timeMS = (delta_time3.sec * delta_time3.prec) + delta_time3.fsec;

  /* Encode and write the frame */
  if(videowriter->Encode(frameimg, timeMS) != 0) {
    Error("Failed encoding video frame");
  }

  /* Add the frame to the timecodes file */
  fprintf(timecodes_fd, "%u\n", timeMS);

  return( true );
}

void Event::updateNotes( const StringSetMap &newNoteSetMap ) {
  bool update = false;

  //Info( "Checking notes, %d <> %d", noteSetMap.size(), newNoteSetMap.size() );
  if ( newNoteSetMap.size() > 0 ) {
    if ( noteSetMap.size() == 0 ) {
      noteSetMap = newNoteSetMap;
      update = true;
    } else {
      for ( StringSetMap::const_iterator newNoteSetMapIter = newNoteSetMap.begin(); newNoteSetMapIter != newNoteSetMap.end(); newNoteSetMapIter++ ) {
        const std::string &newNoteGroup = newNoteSetMapIter->first;
        const StringSet &newNoteSet = newNoteSetMapIter->second;
        //Info( "Got %d new strings", newNoteSet.size() );
        if ( newNoteSet.size() > 0 ) {
          StringSetMap::iterator noteSetMapIter = noteSetMap.find( newNoteGroup );
          if ( noteSetMapIter == noteSetMap.end() ) {
            //Info( "Can't find note group %s, copying %d strings", newNoteGroup.c_str(), newNoteSet.size() );
            noteSetMap.insert( StringSetMap::value_type( newNoteGroup, newNoteSet ) );
            update = true;
          } else {
            StringSet &noteSet = noteSetMapIter->second;
            //Info( "Found note group %s, got %d strings", newNoteGroup.c_str(), newNoteSet.size() );
            for ( StringSet::const_iterator newNoteSetIter = newNoteSet.begin(); newNoteSetIter != newNoteSet.end(); newNoteSetIter++ ) {
              const std::string &newNote = *newNoteSetIter;
              StringSet::iterator noteSetIter = noteSet.find( newNote );
              if ( noteSetIter == noteSet.end() ) {
                noteSet.insert( newNote );
                update = true;
              }
            } // end for
          } // end if ( noteSetMap.size() == 0
        } // end if newNoteSetupMap.size() > 0
      } // end foreach newNoteSetMap
    } // end if have old notes
  } // end if have new notes

  if ( update ) {
    std::string notes;
    createNotes( notes );

    Debug( 2, "Updating notes for event %d, '%s'", id, notes.c_str() );
    static char sql[ZM_SQL_MED_BUFSIZ];
#if USE_PREPARED_SQL
    static MYSQL_STMT *stmt = 0;

    char notesStr[ZM_SQL_MED_BUFSIZ] = "";
    unsigned long notesLen = 0;

    if ( !stmt ) {
      const char *sql = "update Events set Notes = ? where Id = ?";

      stmt = mysql_stmt_init( &dbconn );
      if ( mysql_stmt_prepare( stmt, sql, strlen(sql) ) ) {
        Fatal( "Unable to prepare sql '%s': %s", sql, mysql_stmt_error(stmt) );
      }

      /* Get the parameter count from the statement */
      if ( mysql_stmt_param_count( stmt ) != 2 ) {
        Fatal( "Unexpected parameter count %ld in sql '%s'", mysql_stmt_param_count( stmt ), sql );
      }

      MYSQL_BIND  bind[2];
      memset(bind, 0, sizeof(bind));

      /* STRING PARAM */
      bind[0].buffer_type = MYSQL_TYPE_STRING;
      bind[0].buffer = (char *)notesStr;
      bind[0].buffer_length = sizeof(notesStr);
      bind[0].is_null = 0;
      bind[0].length = &notesLen;

      bind[1].buffer_type= MYSQL_TYPE_LONG;
      bind[1].buffer= (char *)&id;
      bind[1].is_null= 0;
      bind[1].length= 0;

      /* Bind the buffers */
      if ( mysql_stmt_bind_param( stmt, bind ) ) {
        Fatal( "Unable to bind sql '%s': %s", sql, mysql_stmt_error(stmt) );
      }
    }

    strncpy( notesStr, notes.c_str(), sizeof(notesStr) );
    notesLen = notes.length();

    if ( mysql_stmt_execute( stmt ) ) {
      Fatal( "Unable to execute sql '%s': %s", sql, mysql_stmt_error(stmt) );
    }
#else
    static char escapedNotes[ZM_SQL_MED_BUFSIZ];

    mysql_real_escape_string( &dbconn, escapedNotes, notes.c_str(), notes.length() );

    snprintf( sql, sizeof(sql), "update Events set Notes = '%s' where Id = %d", escapedNotes, id );
    if ( mysql_query( &dbconn, sql ) ) {
      Error( "Can't insert event: %s", mysql_error( &dbconn ) );
    }
#endif
  }
}

void Event::AddFrames( int n_frames, Image **images, struct timeval **timestamps ) {
  for (int i = 0; i < n_frames; i += ZM_SQL_BATCH_SIZE) {
    AddFramesInternal(n_frames, i, images, timestamps);
  }
}

void Event::AddFramesInternal( int n_frames, int start_frame, Image **images, struct timeval **timestamps ) {
  static char sql[ZM_SQL_LGE_BUFSIZ];
  strncpy( sql, "insert into Frames ( EventId, FrameId, TimeStamp, Delta ) values ", sizeof(sql) );
  int frameCount = 0;
  for ( int i = start_frame; i < n_frames && i - start_frame < ZM_SQL_BATCH_SIZE; i++ ) {
    if ( !timestamps[i]->tv_sec ) {
      Debug( 1, "Not adding pre-capture frame %d, zero timestamp", i );
      continue;
    }

    frames++;

    static char event_file[PATH_MAX];
    snprintf( event_file, sizeof(event_file), capture_file_format, path, frames );
    if ( monitor->GetOptSaveJPEGs() & 4) {
      //If this is the first frame, we should add a thumbnail to the event directory
      if(frames == 10){
        char snapshot_file[PATH_MAX];
        snprintf( snapshot_file, sizeof(snapshot_file), "%s/snapshot.jpg", path );
        WriteFrameImage( images[i], *(timestamps[i]), snapshot_file );
      }
    }
    if ( monitor->GetOptSaveJPEGs() & 1) {
      Debug( 1, "Writing pre-capture frame %d", frames );
      WriteFrameImage( images[i], *(timestamps[i]), event_file );
    }
    if ( videowriter != NULL ) {
      WriteFrameVideo( images[i], *(timestamps[i]), videowriter );
    }

    struct DeltaTimeval delta_time;
    DELTA_TIMEVAL( delta_time, *(timestamps[i]), start_time, DT_PREC_2 );

    int sql_len = strlen(sql);
    snprintf( sql+sql_len, sizeof(sql)-sql_len, "( %d, %d, from_unixtime(%ld), %s%ld.%02ld ), ", id, frames, timestamps[i]->tv_sec, delta_time.positive?"":"-", delta_time.sec, delta_time.fsec );

    frameCount++;
  }

  if ( frameCount ) {
    Debug( 1, "Adding %d/%d frames to DB", frameCount, n_frames );
    *(sql+strlen(sql)-2) = '\0';
    if ( mysql_query( &dbconn, sql ) ) {
      Error( "Can't insert frames: %s", mysql_error( &dbconn ) );
      exit( mysql_errno( &dbconn ) );
    }
    last_db_frame = frames;
  } else {
    Debug( 1, "No valid pre-capture frames to add" );
  }
}

void Event::AddFrame( Image *image, struct timeval timestamp, int score, Image *alarm_image ) {
  if ( !timestamp.tv_sec ) {
    Debug( 1, "Not adding new frame, zero timestamp" );
    return;
  }

  frames++;

  static char event_file[PATH_MAX];
  snprintf( event_file, sizeof(event_file), capture_file_format, path, frames );

  if ( monitor->GetOptSaveJPEGs() & 4) {
    //If this is the first frame, we should add a thumbnail to the event directory
    if(frames == 10){
      char snapshot_file[PATH_MAX];
      snprintf( snapshot_file, sizeof(snapshot_file), "%s/snapshot.jpg", path );
      WriteFrameImage( image, timestamp, snapshot_file );
    }
  }
  if( monitor->GetOptSaveJPEGs() & 1) {
    Debug( 1, "Writing capture frame %d", frames );
    WriteFrameImage( image, timestamp, event_file );
  }
  if ( videowriter != NULL ) {
    WriteFrameVideo( image, timestamp, videowriter );
  }

  struct DeltaTimeval delta_time;
  DELTA_TIMEVAL( delta_time, timestamp, start_time, DT_PREC_2 );

  const char *frame_type = score>0?"Alarm":(score<0?"Bulk":"Normal");
  if ( score < 0 )
    score = 0;

  bool db_frame = (strcmp(frame_type,"Bulk") != 0) || ((frames%config.bulk_frame_interval)==0) || !frames;
  if ( db_frame ) {

    Debug( 1, "Adding frame %d of type \"%s\" to DB", frames, frame_type );
    static char sql[ZM_SQL_MED_BUFSIZ];
    snprintf( sql, sizeof(sql), "insert into Frames ( EventId, FrameId, Type, TimeStamp, Delta, Score ) values ( %d, %d, '%s', from_unixtime( %ld ), %s%ld.%02ld, %d )", id, frames, frame_type, timestamp.tv_sec, delta_time.positive?"":"-", delta_time.sec, delta_time.fsec, score );
    if ( mysql_query( &dbconn, sql ) ) {
      Error( "Can't insert frame: %s", mysql_error( &dbconn ) );
      exit( mysql_errno( &dbconn ) );
    }
    last_db_frame = frames;

    // We are writing a Bulk frame
    if ( !strcmp( frame_type,"Bulk") ) {
      snprintf( sql, sizeof(sql), "update Events set Length = %s%ld.%02ld, Frames = %d, AlarmFrames = %d, TotScore = %d, AvgScore = %d, MaxScore = %d where Id = %d", 
          ( delta_time.positive?"":"-" ),
          delta_time.sec, delta_time.fsec,
          frames, 
          alarm_frames,
          tot_score,
          (int)(alarm_frames?(tot_score/alarm_frames):0),
          max_score,
          id
          );
      if ( mysql_query( &dbconn, sql ) ) {
        Error( "Can't update event: %s", mysql_error( &dbconn ) );
        exit( mysql_errno( &dbconn ) );
      }
    }
  }

  end_time = timestamp;

  // We are writing an Alarm frame
  if ( !strcmp( frame_type,"Alarm") ) {
    alarm_frames++;

    tot_score += score;
    if ( score > (int)max_score )
      max_score = score;

    if ( alarm_image ) {
      snprintf( event_file, sizeof(event_file), analyse_file_format, path, frames );

      Debug( 1, "Writing analysis frame %d", frames );
      if ( monitor->GetOptSaveJPEGs() & 2) {
        WriteFrameImage( alarm_image, timestamp, event_file, true );
      }
    }
  }

  /* This makes viewing the diagnostic images impossible because it keeps deleting them
  if ( config.record_diag_images ) {
    char diag_glob[PATH_MAX] = "";

    snprintf( diag_glob, sizeof(diag_glob), "%s/%d/diag-*.jpg", staticConfig.DIR_EVENTS.c_str(), monitor->Id() );
    glob_t pglob;
    int glob_status = glob( diag_glob, 0, 0, &pglob );
    if ( glob_status != 0 ) {
      if ( glob_status < 0 ) {
        Error( "Can't glob '%s': %s", diag_glob, strerror(errno) );
      } else {
        Debug( 1, "Can't glob '%s': %d", diag_glob, glob_status );
      }
    } else {
      char new_diag_path[PATH_MAX] = "";
      for ( int i = 0; i < pglob.gl_pathc; i++ ) {
        char *diag_path = pglob.gl_pathv[i];

        char *diag_file = strstr( diag_path, "diag-" );

        if ( diag_file ) {
          snprintf( new_diag_path, sizeof(new_diag_path), general_file_format, path, frames, diag_file );

          if ( rename( diag_path, new_diag_path ) < 0 ) {
            Error( "Can't rename '%s' to '%s': %s", diag_path, new_diag_path, strerror(errno) );
          }
        }
      }
    }
    globfree( &pglob );
  }
  */
}
