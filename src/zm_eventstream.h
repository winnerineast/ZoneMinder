//
// ZoneMinder Core Interfaces, $Date$, $Revision$
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

#ifndef ZM_EVENTSTREAM_H
#define ZM_EVENTSTREAM_H

#include <set>
#include <map>

#include "zm_image.h"
#include "zm_stream.h"
#include "zm_video.h"

class EventStream : public StreamBase {
public:
  typedef enum { MODE_SINGLE, MODE_ALL, MODE_ALL_GAPLESS } StreamMode;

protected:
  struct FrameData {
    //unsigned long   id;
    time_t      timestamp;
    time_t      offset;
    double      delta;
    bool      in_db;
  };

  struct EventData {
    unsigned long   event_id;
    unsigned long   monitor_id;
    unsigned long   frame_count;
    time_t          start_time;
    double          duration;
    char            path[PATH_MAX];
    int             n_frames;
    FrameData       *frames;
    char            video_file[PATH_MAX];
  };

protected:
  static const int STREAM_PAUSE_WAIT = 250000; // Microseconds

  static const StreamMode DEFAULT_MODE = MODE_SINGLE;

protected:
  StreamMode mode;
  bool forceEventChange;

  int curr_frame_id;
  double curr_stream_time;

  EventData *event_data;

protected:
  bool loadEventData( int event_id );
  bool loadInitialEventData( int init_event_id, unsigned int init_frame_id );
  bool loadInitialEventData( int monitor_id, time_t event_time );

  void checkEventLoaded();
  void processCommand( const CmdMsg *msg );
  bool sendFrame( int delta_us );

public:
  EventStream() {
    mode = DEFAULT_MODE;

    forceEventChange = false;

    curr_frame_id = 0;
    curr_stream_time = 0.0;

    event_data = 0;
  }
  void setStreamStart( int init_event_id, unsigned int init_frame_id=0 ) {
    loadInitialEventData( init_event_id, init_frame_id );
    loadMonitor( event_data->monitor_id );
  }
  void setStreamStart( int monitor_id, time_t event_time ) {
    loadInitialEventData( monitor_id, event_time );
    loadMonitor( monitor_id );
  }
  void setStreamMode( StreamMode p_mode ) {
    mode = p_mode;
  }
  void runStream();
};

#endif // ZM_EVENTSTREAM_H
