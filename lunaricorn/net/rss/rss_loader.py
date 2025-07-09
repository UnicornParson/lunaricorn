from ..content_loader import ContentLoader
from dataclasses import dataclass, fields, asdict
from typing import List, Optional, Dict, Any
from ..content_type import ContentType
import logging
import time
import feedparser
from ...data.convertor import DataConvertor
from urllib.parse import urlparse, urlunparse
import hashlib

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


class RssLoader(ContentLoader):
    def __init__(self):
        super().__init__()
        self.logger = logging.getLogger("net.RssLoader")

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
        for entry in feed.entries:
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

    async def load(self, url: str) -> List[NewsEntry]:
        start = time.time()
        content_type, raw_content = await super().fetch(url)

        if content_type == ContentType.RAW_ATOM:
            entries = self._parse_atom(raw_content)
            for entry in entries:
                entry = await self._fetch_entry(entry)
                break
        elif content_type == ContentType.RAW_RSS:
            entries = self._parse_rss(raw_content)
            for entry in entries:
                entry = await self._fetch_entry(entry)
        else:
            self.logger.warning(f"Unknown content type: {content_type}")
            return []
        
        end = time.time()
        self.logger.info(f"Loaded {len(entries)} entries in {end - start} seconds")
        return entries

