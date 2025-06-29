from bs4 import BeautifulSoup

def is_html_valid(input_str: str) -> bool:
    soup = BeautifulSoup(input_str, 'html.parser')
    return bool(soup.find() or 
                soup.contents or 
                isinstance(soup, BeautifulSoup))