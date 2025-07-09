from enum import Enum
import html
import re
import logging

# Configure logger for this module
logger = logging.getLogger(__name__)


class ContentType(Enum):
    """
    Enumeration of different content types that can be detected in web content.
    
    This enum defines various content formats that the system can identify,
    including RSS feeds, Atom feeds, HTML-wrapped content, and other formats.
    """
    INVALID = "invalid" 
    RAW_RSS = "raw_rss"           # Raw RSS/XML without wrapper
    HTML_RSS = "html_rss"         # RSS wrapped in HTML
    RAW_ATOM = "raw_atom"         # Raw Atom/XML without wrapper
    HTML_ATOM = "html_atom"       # Atom wrapped in HTML
    ANUBIS = "anubis"             # Anubis verification page
    PLAIN_TEXT = "plain_text"     # Plain text content
    OTHER = "other"               # Other content type


def detect_content_type(content: str) -> ContentType:
    """
    Detects the type of content in a string.
    
    This function analyzes the content and determines its type using
    various detection methods. It checks for RSS feeds, Atom feeds,
    Anubis pages, plain text, and other content types.
    
    Args:
        content (str): The content string to analyze
        
    Returns:
        ContentType: The detected content type as a ContentType enum value
    """
    if not content:
        return ContentType.OTHER
    
    # Anubis detector
    if is_anubis_page(content):
        return ContentType.ANUBIS
    
    # Check for raw formats first
    if is_raw_atom(content):
        return ContentType.RAW_ATOM
    if is_raw_rss(content):
        return ContentType.RAW_RSS

    # Check for HTML-wrapped formats
    if is_html_wrapped_rss(content):
        return ContentType.HTML_RSS
    if is_html_wrapped_atom(content):
        return ContentType.HTML_ATOM
    
    # Plain text detector
    if is_plain_text(content):
        return ContentType.PLAIN_TEXT
    
    return ContentType.OTHER


def is_anubis_page(content: str) -> bool:
    """
    Checks if the content is an Anubis verification page.
    
    Anubis is a bot protection service that shows verification pages
    to ensure the user is not a bot. This function detects such pages
    by looking for specific indicators in the content.
    
    Args:
        content (str): The HTML content to check
        
    Returns:
        bool: True if the content appears to be an Anubis page, False otherwise
    """
    content = html.unescape(content)
    anubis_indicators = [
        "Making sure you're not a bot!",
        "Protected by Anubis",
        "id=\"progress\"",
        "id=\"status\"",
        "anubis_version",
        "/anubis/static/img/"
    ]
    return any(indicator in content for indicator in anubis_indicators)


def is_raw_rss(content: str) -> bool:
    """
    Checks if the content is raw RSS/XML format.
    
    Raw RSS content starts with XML declaration or RSS tag and contains
    RSS-specific elements without being wrapped in HTML.
    
    Args:
        content (str): The content to check
        
    Returns:
        bool: True if the content is raw RSS, False otherwise
    """
    content = html.unescape(content)
    return content.lstrip().startswith(('<?xml', '<rss')) and ("<rss" in content)


def is_html_wrapped_rss(content: str) -> bool:
    """
    Checks if the content is RSS wrapped in HTML.
    
    This function detects RSS content that is embedded within HTML pages,
    typically found when RSS feeds are displayed in web browsers.
    
    Args:
        content (str): The content to check
        
    Returns:
        bool: True if the content is RSS wrapped in HTML, False otherwise
    """
    html_indicators = ['<html', '<head', '<body']
    return (not is_raw_rss(content)) and ("<rss" in content) and any(indicator in content for indicator in html_indicators)


def is_raw_atom(content: str) -> bool:
    """
    Checks if the content is raw Atom/XML format.
    
    Raw Atom content starts with XML declaration or feed tag and contains
    Atom-specific elements without being wrapped in HTML.
    
    Args:
        content (str): The content to check
        
    Returns:
        bool: True if the content is raw Atom, False otherwise
    """
    content = html.unescape(content)
    if not content.lstrip().startswith(('<?xml', '<feed')):
        return False

    atom_indicators = [
        '<feed', '</feed>',
        'xmlns="http://www.w3.org/2005/Atom"',
        '<entry>', '</entry>',
        '<id>', '</id>',
        '<title>', '</title>',
        '<updated>', '</updated>'
    ]
    
    return all(indicator in content for indicator in atom_indicators)


def is_html_wrapped_atom(content: str) -> bool:
    """
    Checks if the content is Atom wrapped in HTML.
    
    This function detects Atom content that is embedded within HTML pages,
    typically found when Atom feeds are displayed in web browsers.
    
    Args:
        content (str): The content to check
        
    Returns:
        bool: True if the content is Atom wrapped in HTML, False otherwise
    """
    content = html.unescape(content)
    html_indicators = ['<html', '<head', '<body', '<meta', '<div', '<pre']
    if not any(indicator in content for indicator in html_indicators):
        return False

    atom_indicators = ['<feed', '</feed>', '<entry>', 'xmlns="http://www.w3.org/2005/Atom"']
    return any(indicator in content for indicator in atom_indicators)


def is_plain_text(content: str) -> bool:
    """
    Checks if the content is plain text.
    
    This function uses heuristics to determine if the content is primarily
    plain text rather than HTML or other structured formats.
    
    Args:
        content (str): The content to check
        
    Returns:
        bool: True if the content appears to be plain text, False otherwise
    """
    content = html.unescape(content)
    tags = re.findall(r'<[^>]+>', content)
    tag_ratio = len(tags) / (len(content) / 100) if content else 0

    text_indicators = [
        len(content.splitlines()) > 10,  # Many lines
        '\n' in content,                 # Line breaks
        tag_ratio < 5,                   # Few tags
        not content.startswith('<')       # Doesn't start with a tag
    ]
    return all(text_indicators)


def extract_xml(html_str: str) -> str:
    """
    Extracts XML content from HTML-wrapped XML.
    
    This function attempts to extract clean XML content from HTML pages
    that contain embedded XML (typically RSS or Atom feeds).
    
    Args:
        html_str (str): HTML string that may contain XML content
        
    Returns:
        str: Extracted XML content, or empty string if no XML found
    """
    html_str = html.unescape(html_str)
    start_marker = "<?xml"
    end_marker = "</feed>"

    start_idx = html_str.find(start_marker)
    if start_idx == -1:
        return ""

    end_idx = html_str.find(end_marker, start_idx)
    
    # Extract substring (including end_marker length)
    if end_idx == -1:
        xml_escaped = html_str[start_idx:]  # To end of string
    else:
        xml_escaped = html_str[start_idx:end_idx + len(end_marker)]
    
    return xml_escaped