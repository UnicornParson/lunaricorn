import lunaricorn
import lunaricorn.api.signaling as lsig
from pathlib import Path
from datetime import datetime
import logging
import os
import sqlite3
import uuid
from pathlib import Path
import time

class RSSLoaderClient():
    def __init__(self, agent_id:str, working_dir:str):
        
        self.agent_id = agent_id.replace(":", "").replace("\\", "").replace("/", "").replace("..", "_")
        if not self.agent_id:
            self.agent_id = f"RSSLoaderClient_{str(uuid.uuid4())}"
        if not os.path.isdir(working_dir):
            raise ValueError ("{working_dir} is not a valid folder")
        self.working_dir = Path(working_dir)
        self.sig_config = None
        self.sig_client = None
        self.logger = logging.getLogger(__name__)
        self.dump_path = self.working_dir / f"{self.agent_id}_dump.txt"
        self.dumper = lunaricorn.net.FileDataDumper(self.dump_path)
        self.logger.info(f"dump to {self.dump_path}")
        self.pending_folder = self.working_dir / "pending"
        self.pending_folder.mkdir(exist_ok=True)
        self.index_db = self.working_dir / "index.db"
        self.conn = None
        self.cursor = None

        # try to install index
        self._open_index()
        if not self._is_db_connected():
            raise ConnectionError("index db not connected")
        self._close_index()


    def _is_db_connected(self) -> bool:
        return self.conn and self.cursor
    
    def _close_index(self):
        if self.cursor:
            self.cursor.close()
            self.cursor = None
        
        if self.conn:
            try:
                self.conn.commit()
            except sqlite3.ProgrammingError:
                pass
            finally:
                self.conn.close()
                self.conn = None

    def _open_index(self):
        self._close_index()
        need_init = not os.path.exists(self.index_db)
        self.conn = sqlite3.connect(self.index_db)
        self.cursor = self.conn.cursor()
        
        
        
        if need_init:
            self.logger.info("install db")
            self.cursor.execute("PRAGMA auto_vacuum = 2")
            self.cursor.execute("PRAGMA checkpoint_fullfsync = 1")
            self.cursor.execute("PRAGMA journal_mode = WAL")
            
            self.cursor.execute('''
                CREATE TABLE "index" (
                    "id" INTEGER NOT NULL UNIQUE,
                    "content_utl" TEXT NOT NULL UNIQUE,
                    "last_seen" REAL NOT NULL,
                    PRIMARY KEY("id" AUTOINCREMENT)
                )
            ''')
            self.conn.commit()
    def _already_processed(self, url) -> bool:
        if not self._is_db_connected():
            raise ConnectionError("index db not connected")
        
        try:
            current_time = time.time()

            self.cursor.execute(
                'UPDATE "index" SET last_seen = ? WHERE content_utl = ?',
                (current_time, url)
            )
            

            if self.cursor.rowcount > 0:
                self.conn.commit()
                return True
            else:
                return False
                
        except Exception as e:
            if hasattr(self, 'conn') and self.conn:
                self.conn.rollback()
            self.logger.error(f"Failed to update index: {e} for {url}")
            return False
        
    def connect_to_lunaricorn(self, sig_config:lsig.SignalingClientConfig):
        self.sig_config = sig_config

        self.sig_client = lsig.SignalingClient(self.sig_config, self.agent_id)
        rc = self.sig_client.connect()
        if not rc:
            self.sig_client.disconnect()
            self.sig_client = None
            raise ConnectionError(f"cannot connect to signaling server {str(self.sig_config)}")
        
    def disconnect_lunaricorn(self):
        if self.sig_client:
            self.sig_client.disconnect()
            self.sig_client = None

    def _push_record(self, url):
        if not self._is_db_connected():
            raise ConnectionError("index db not connected")
            
        try:
            current_time = time.time()
            
            # Insert new record
            self.cursor.execute(
                'INSERT INTO "index" (content_utl, last_seen) VALUES (?, ?)',
                (url, current_time)
            )
            self.conn.commit()
            return True
            
        except sqlite3.IntegrityError:
            # Unique constraint violation - record already exists
            # This shouldn't happen if we check with already_processed first
            self.logger.error(f"Record with URL {url} already exists in database")
            self.conn.rollback()
            return False
            
        except Exception as e:
            # Other errors
            self.logger.error(f"Error adding URL {url}: {e}")
            if self.conn:
                self.conn.rollback()
            return False

    async def load(self, url) -> list:
        self._open_index()
        if not self._is_db_connected():
            raise ConnectionError("index db not connected")
        rc = []
        class CustomDbEngine():
            def __init__(self, impl):
                self.impl = impl
                self.pushed = 0
                self.skipped = 0
            def has_entry(self, content_hash: str, content_url: str) -> bool:
                rc = self.impl._already_processed(content_url)
                if rc:
                    self.skipped += 1
                return rc
            def add_entry(self, content_hash: str, link: str, title: str, updated: str):
                self.impl._push_record(link)
                self.pushed += 1
        try:
            if self.sig_client:
                # notify lunaricorn
                self.logger.info(f"notify lunaricorn about entry")
                self.sig_client.push_event(event_type = lsig.SignalingEventType.Robots.value,
                                            payload = {"job_enter": "rss_loader", "url": url},
                                            source=self.agent_id,
                                            tags=[lsig.SignalingEventFlags.JobEntry.value]
                                            )
            engine = CustomDbEngine(self)
            loader = lunaricorn.net.rss.RssLoader(url, dumper=self.dumper, db_engine = engine)
            entries = await loader.load()
            self.logger.info(f"entries fetched: {len(entries)}")
            for e in entries:
                md = e.export_to_md()
                convertor = lunaricorn.data.DataConvertor()
                dt = datetime.now().strftime("%Y%m%d%H%M%S%f")[:-3]
                safe_fname = convertor.str_to_valid_filename(e.title, allow_unicode = True, max_length=128)
                filename = self.pending_folder / f"{safe_fname}.{dt}.md"
                self.logger.info(f"save md {filename}")
                with open(filename, "w") as f:
                    f.write(md)
                    f.flush()
                rc.append((e.title, str(filename)))
                if self.sig_client:
                    # notify lunaricorn
                    self.logger.info(f"notify lunaricorn about {e.title}")
                    self.sig_client.push_event(event_type = lsig.SignalingEventType.Robots.value,
                                               payload = {"title": e.title, "filename": str(filename), "dt": dt},
                                               source=self.agent_id,
                                               tags=[lsig.SignalingEventFlags.News.value, lsig.SignalingEventFlags.Md.value, lsig.SignalingEventFlags.Obsidian.value]
                                               )
                    
            # on done
            if self.sig_client:
                # notify lunaricorn
                self.logger.info(f"notify lunaricorn about entry")
                self.sig_client.push_event(event_type = lsig.SignalingEventType.Robots.value,
                                            payload = {"job_exit": "rss_loader", "url": url},
                                            source=self.agent_id,
                                            tags=[lsig.SignalingEventFlags.JobExit.value]
                                            )
        except Exception as e:
            self.logger.error(f"loading error {e}")
            rc = []
        finally:
            self._close_index()
        return rc