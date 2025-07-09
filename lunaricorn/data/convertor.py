import base64
import re
import requests
from bs4 import BeautifulSoup
from markdownify import markdownify as md
from urllib.parse import urljoin
import logging
import unicodedata

class DataConvertor:

    def __init__(self):
        self.logger = logging.getLogger("data.DataConvertor")

    def str_to_valid_filename(self, s: str, max_length: int = 255, replace_spaces: str = '_', allow_unicode: bool = False) -> str:
        if allow_unicode:
            s = unicodedata.normalize('NFKC', s)
        else:
            s = unicodedata.normalize('NFKD', s).encode('ascii', 'ignore').decode('ascii')
        s = re.sub(r'\s+', replace_spaces, s)
        if allow_unicode:
            s = re.sub(r'[^\w\s-]', '', s, flags=re.U)
        else:
            s = re.sub(r'[^\w\s-]', '', s)
        s = s.strip(' .')
        windows_reserved = [
            "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", 
            "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", 
            "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
        ]
        if s.upper() in windows_reserved:
            s = '_' + s
        return s[:max_length]

    def html_to_markdown(self, html_content, base_url="", metadata=None):
        """
        Converts HTML to Markdown, replacing all images with base64 data URI.
        
        :param html_content: Source HTML code
        :param base_url: Base URL for resolving relative paths
        :param metadata: Optional dictionary to be inserted at the beginning as key-value list
        :return: Markdown with base64 images and optional metadata
        """
        soup = BeautifulSoup(html_content, 'html.parser')
        
        # Process all images
        for img in soup.find_all('img'):
            src = img.get('src', '')
            if not src:
                continue
            
            # Get image content
            img_data = self._fetch_image_content(src, base_url)
            if not img_data:
                continue
            
            # Determine MIME type and encode to base64
            mime_type = self._get_image_mimetype(src)
            base64_str = base64.b64encode(img_data).decode('utf-8')
            img['src'] = f"data:{mime_type};base64,{base64_str}"
        
        # Convert modified HTML to Markdown
        markdown_content = md(str(soup))
        
        # Add metadata at the beginning if provided
        if metadata and isinstance(metadata, dict):
            metadata_lines = []
            for key, value in metadata.items():
                metadata_lines.append(f"**{key}:** {value}")
            
            if metadata_lines:
                metadata_section = "\n".join(metadata_lines)
                markdown_content = f"{metadata_section}\n---\n{markdown_content}"
        
        return markdown_content

    def _fetch_image_content(self, src, base_url):
        """ Gets binary content of the image """
        try:
            # Handle data URI (already encoded images)
            if src.startswith('data:image'):
                match = re.search(r'base64,(.*)', src)
                return base64.b64decode(match.group(1)) if match else None
            
            # Resolve relative paths
            full_url = urljoin(base_url, src)
            
            # Download via HTTP or read local file
            if full_url.startswith(('http://', 'https://')):
                response = requests.get(full_url, timeout=10)
                response.raise_for_status()
                return response.content
            else:
                with open(full_url, 'rb') as f:
                    return f.read()
                    
        except Exception as e:
            self.logger.error(f"Error processing image {src}: {str(e)}")
            return None

    def _get_image_mimetype(self, src):
        """ Determines MIME type by file extension """
        if src.startswith('data:'):
            match = re.search(r'data:(image\/\w+);', src)
            return match.group(1) if match else 'image/jpeg'
        
        ext = src.split('.')[-1].lower()
        return {
            'png': 'image/png',
            'jpg': 'image/jpeg',
            'jpeg': 'image/jpeg',
            'gif': 'image/gif',
            'svg': 'image/svg+xml',
            'webp': 'image/webp'
        }.get(ext, 'image/jpeg')  # fallback