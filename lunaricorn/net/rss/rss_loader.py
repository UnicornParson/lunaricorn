from ..content_loader import ContentLoader
from dataclasses import dataclass, fields, asdict
from typing import List, Optional, Dict, Any
from ..content_type import *
import logging
import time
import feedparser
from ...data.convertor import DataConvertor
from urllib.parse import urlparse, urlunparse
import hashlib
import sqlite3
import os
import tqdm
import traceback
from ..data_dumper import *

@dataclass
class NewsEntry:
    title: str
    link: str
    id: str
    updated: str
    summary: Optional[str] = None
    content: Optional[str] = None
    full_content: Optional[str] = None
    full_content_type: Optional[ContentType] = None

    @classmethod
    def get_field_names(cls) -> List[str]:
        return [f.name for f in fields(cls)]
    
    def to_table_row(
        self,
        fields_list: Optional[List[str]] = None,
        max_length: int = 50,
        truncate: bool = True
    ) -> List[str]:
        # If fields are not specified, use all available fields
        if fields_list is None:
            fields_list = [f.name for f in fields(self.__class__)]
        
        row = []
        for field in fields_list:
            # Get field value
            value = getattr(self, field, "")
            
            # Handle None values
            if value is None:
                row.append("")
                continue
                
            # Convert to string
            value_str = str(value)
            
            # Truncate long text
            if truncate and len(value_str) > max_length:
                value_str = value_str[:max_length-3] + "..."
                
            row.append(value_str)
        return row

    def export_to_md(self) -> str:
        """
        Exports this NewsEntry object to a Markdown string using DataConvertor.
        Always includes metadata at the top of the entry.

        :return: Markdown string representing this news entry
        """
        convertor = DataConvertor()

        # Extract base_url from self.link
        base_url = ""
        if self.link:
            parsed = urlparse(self.link)
            # Build base URL: scheme://netloc/
            base_url = urlunparse((parsed.scheme, parsed.netloc, '/', '', '', ''))

        # Always include metadata
        metadata = {
            "title": self.title,
            "link": self.link,
            "id": self.id,
            "updated": self.updated,
            "content_hash": hashlib.sha256(self.full_content.encode()).hexdigest() if self.full_content else "None"
        }
        # Optionally add summary as metadata if present

        # Prefer full_content, then content, then summary
        html_content = self.full_content or self.content or self.summary or ""
        # Convert HTML to Markdown
        md = convertor.html_to_markdown(html_content, base_url=base_url, metadata=metadata)
        return md


class RssEntryDB:
    """
    SQLite helper for storing and checking already loaded RSS entries by content_hash.
    """
    def __init__(self, db_path="rss_entries.db"):
        self.db_path = db_path
        self._ensure_db()

    def _ensure_db(self):
        # Ensure the database and table exist
        if os.path.dirname(self.db_path):
            os.makedirs(os.path.dirname(self.db_path), exist_ok=True)
        with sqlite3.connect(self.db_path) as conn:
            conn.execute(
                """
                CREATE TABLE IF NOT EXISTS rss_entries (
                    content_hash TEXT PRIMARY KEY,
                    link TEXT,
                    title TEXT,
                    updated TEXT,
                    date_added TIMESTAMP DEFAULT CURRENT_TIMESTAMP
                )
                """
            )

    def has_entry(self, content_hash: str) -> bool:
        # Check if an entry with the given content_hash exists
        with sqlite3.connect(self.db_path) as conn:
            cur = conn.execute("SELECT 1 FROM rss_entries WHERE content_hash = ?", (content_hash,))
            return cur.fetchone() is not None

    def add_entry(self, content_hash: str, link: str, title: str, updated: str):
        # Add a new entry to the database
        with sqlite3.connect(self.db_path) as conn:
            conn.execute(
                "INSERT OR IGNORE INTO rss_entries (content_hash, link, title, updated) VALUES (?, ?, ?, ?)",
                (content_hash, link, title, updated)
            )


class RssLoader(ContentLoader):
    def __init__(self, url: str, db_dir: str = None, dumper: IDataDumper = EmptyDataDumper(), db_engine = None):
        super().__init__()
        self.logger = logging.getLogger("net.RssLoader")
        # Use a separate database file for each RSS feed, based on the hash of the URL
        url_hash = hashlib.sha256(url.encode()).hexdigest()[:16]
        db_filename = f"rss_entries_{url_hash}.db"
        if db_dir is None:
            db_dir = os.getcwd()
        db_path = os.path.join(db_dir, db_filename)
        self.db = db_engine
        if not self.db:
            self.db = RssEntryDB(db_path)
        self.feed_url = url
        self.dumper = dumper
        
    def _parse_atom(self, xml_content: str) -> List[NewsEntry]:
        # Parse XML using feedparser
        feed = feedparser.parse(xml_content)
        entries = []
        
        for entry in feed.entries:
            # Handle content (can be in multiple formats)
            content_value = None
            if hasattr(entry, 'content'):
                for item in entry.content:
                    if hasattr(item, 'value'):
                        content_value = item.value
                        break
            
            # Handle update date
            updated_str = ""
            if hasattr(entry, 'updated_parsed') and entry.updated_parsed:
                try:
                    # Check if updated_parsed is a proper time tuple
                    if isinstance(entry.updated_parsed, (tuple, list)) and len(entry.updated_parsed) >= 6:
                        # Convert list to tuple if needed for time.strftime
                        time_tuple = tuple(entry.updated_parsed) if isinstance(entry.updated_parsed, list) else entry.updated_parsed
                        updated_str = time.strftime('%Y-%m-%dT%H:%M:%SZ', time_tuple)
                    else:
                        # Fallback to string representation
                        updated_str = str(entry.updated_parsed)
                except (TypeError, ValueError):
                    # If strftime fails, use string representation
                    updated_str = str(entry.updated_parsed)
            elif hasattr(entry, 'updated'):
                updated_str = entry.updated
            
            # Create entry object with safe attribute access
            entries.append(NewsEntry(
                title=entry.get('title', ''),
                link=entry.get('link', ''),
                id=entry.get('id', ''),
                updated=updated_str,
                summary=entry.get('summary'),
                content=content_value
            ))
        
        return entries
    
    def _parse_rss(self, xml_content: str) -> List[NewsEntry]:
        feed = feedparser.parse(xml_content)
        entries = []
        for entry in tqdm.tqdm(feed.entries, desc="Parsing RSS entries"):
            entries.append(NewsEntry(
                title=entry.get('title', ''),
                link=entry.get('link', ''),
                id=entry.get('id', ''),
                updated=entry.get('updated', ''),
                summary=entry.get('summary'),
                content=entry.get('content')
            ))
        return entries

    async def _fetch_entry(self, entry: NewsEntry) -> NewsEntry:
        if not entry.link:
            return entry
        entry.full_content_type, entry.full_content = await super().fetch(entry.link)
        return entry

    async def load(self) -> List[NewsEntry]:
        # Load RSS entries, skipping those already present in the database
        
        content_type, raw_content = await super().fetch(self.feed_url)
        return await self.load_raw(content_type, raw_content)

    async def load_raw(self, content_type, raw_content, depth_limit: int = 10) -> List[NewsEntry]:
        if depth_limit <= 0:
            self.logger.warning(f"Depth limit reached for {self.feed_url}")
            return []
        # TODO: merge blocks, ectract common code
        if content_type == ContentType.RAW_ATOM:
            entries = self._parse_atom(raw_content)
            filtered_entries = []
            for entry in tqdm.tqdm(entries, desc="Loading RSS entries"):
                # Calculate content_hash for uniqueness
                content = entry.full_content or entry.content or entry.summary or ""
                if isinstance(content, list):
                    content = "".join(str(x) for x in content)
                elif not isinstance(content, str):
                    content = str(content)
                content_hash = hashlib.sha256(content.encode()).hexdigest() if content else None
                if content_hash and self.db.has_entry(content_hash, entry.link ):
                    continue  # Already loaded, skip
                entry = await self._fetch_entry(entry)
                # Recalculate content_hash after fetching full_content
                content = entry.full_content or entry.content or entry.summary or ""
                if isinstance(content, list):
                    content = "".join(str(x) for x in content)
                elif not isinstance(content, str):
                    content = str(content)
                content_hash = hashlib.sha256(content.encode()).hexdigest() if content else None
                if content_hash:
                    self.db.add_entry(content_hash, entry.link, entry.title, entry.updated)
                filtered_entries.append(entry)
            entries = filtered_entries
        elif content_type == ContentType.RAW_RSS:
            entries = self._parse_rss(raw_content)
            filtered_entries = []
            for entry in tqdm.tqdm(entries, desc="Loading RSS entries"):
                content = entry.full_content or entry.content or entry.summary or ""
                if isinstance(content, list):
                    content = "".join(str(x) for x in content)
                elif not isinstance(content, str):
                    content = str(content)
                content_hash = hashlib.sha256(content.encode()).hexdigest() if content else None
                if content_hash and self.db.has_entry(content_hash, entry.link):
                    continue
                try:
                    entry = await self._fetch_entry(entry)
                except Exception as e:
                    self.logger.warning(f"Failed to fetch entry: {e}\n{traceback.format_exc()}")
                    self.logger.warning(f"skipping entry: {entry.link}")
                    continue
                content = entry.full_content or entry.content or entry.summary or ""
                if isinstance(content, list):
                    content = "".join(str(x) for x in content)
                elif not isinstance(content, str):
                    content = str(content)
                content_hash = hashlib.sha256(content.encode()).hexdigest() if content else None
                if content_hash:
                    self.db.add_entry(content_hash, entry.link, entry.title, entry.updated)
                filtered_entries.append(entry)
            entries = filtered_entries
        elif content_type == ContentType.HTML_RSS:
            # Handle HTML-wrapped RSS using extractor
            try:
                self.dumper.dump(raw_content)
                self.logger.warning(f"try to reextract: {content_type}")
                extracted_xml = extract_xml(raw_content)
                content_type = detect_content_type(extracted_xml)
                if not extracted_xml or (content_type != ContentType.RAW_RSS and content_type != ContentType.RAW_ATOM):
                    extracted_xml = extract_rss(raw_content)
                    content_type = detect_content_type(extracted_xml)
                if not extracted_xml or (content_type != ContentType.RAW_RSS and content_type != ContentType.RAW_ATOM):
                    self.logger.warning(f"Failed to extract XML from HTML_RSS: \n{raw_content[:1000]}")
                    return []
                
                return await self.load_raw(content_type, extracted_xml, depth_limit - 1)

            except Exception as e:
                self.logger.warning(f"Failed to extract and parse HTML_RSS: {e}\n{traceback.format_exc()}")
                return []
        else:
            # Unknown content type
            self.dumper.dump(raw_content)
            self.logger.warning(f"Unknown content type: {content_type}")
            return []

        self.logger.info(f"Loaded {len(entries)} entries")
        return entries

