import requests
import json
from datetime import datetime

def post_to_endpoint():
    # URL to connect to (both HTTP and HTTPS versions)
    url_http = "http://6f8d-2400-79e0-9041-343-cd1e-6b10-f1a1-1e87.ngrok-free.app/data"
    url_https = "https://6f8d-2400-79e0-9041-343-cd1e-6b10-f1a1-1e87.ngrok-free.app/data"
    
    # The JSON payload to send
    payload = {
        "temperature": 23.5,
        "humidity": 60.0,
        "tempAlert": False,
        "humidityAlert": True,
        "timestamp": "2025-03-28T12:34:56Z"
    }
    
    # Headers
    headers = {
        "Content-Type": "application/json"
    }
    
    print("Attempting to send POST request to HTTP endpoint...")
    try:
        # Try HTTP first
        response = requests.post(url_http, 
                                json=payload, 
                                headers=headers, 
                                allow_redirects=True)  # Automatically follow redirects
        
        print(f"Status Code: {response.status_code}")
        print(f"Response: {response.text}")
        
        if response.status_code != 200:
            print("\nHTTP request didn't return 200, trying HTTPS...")
            response = requests.post(url_https, 
                                    json=payload, 
                                    headers=headers)
            
            print(f"HTTPS Status Code: {response.status_code}")
            print(f"HTTPS Response: {response.text}")
            
    except Exception as e:
        print(f"Error: {e}")
        print("\nAttempting HTTPS endpoint as fallback...")
        try:
            response = requests.post(url_https, 
                                    json=payload, 
                                    headers=headers)
            
            print(f"HTTPS Status Code: {response.status_code}")
            print(f"HTTPS Response: {response.text}")
        except Exception as e:
            print(f"HTTPS Error: {e}")
    
    print("\nConnection Details:")
    print(f"URL: {response.url}")
    print(f"Apparent Encoding: {response.apparent_encoding}")
    print(f"Headers: {dict(response.headers)}")

if __name__ == "__main__":
    post_to_endpoint()