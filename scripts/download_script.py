import requests

url = "https://raw.githubusercontent.com/The-Z-Labs/linux-exploit-suggester/master/linux-exploit-suggester.sh"
try:
    response = requests.get(url)
    response.raise_for_status()
    with open("linux-exploit-suggester.sh", "wb") as f:
        f.write(response.content)
    print("Download successful")
except Exception as e:
    print(f"Error: {e}")
