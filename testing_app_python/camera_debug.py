import cv2
import time
import platform

def test_camera(backend_name, backend_id):
    print(f"\n--- Testing Backend: {backend_name} ---")
    cap = cv2.VideoCapture(0, backend_id)
    if not cap.isOpened():
        print(f"FAILED: Could not open camera with {backend_name}")
        return False
    
    # Try to set properties
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
    
    # Check MJPG
    print("Setting MJPG...")
    cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*'MJPG'))
    
    actual_w = cap.get(cv2.CAP_PROP_FRAME_WIDTH)
    actual_h = cap.get(cv2.CAP_PROP_FRAME_HEIGHT)
    actual_fourcc = int(cap.get(cv2.CAP_PROP_FOURCC))
    codec = "".join([chr((actual_fourcc >> 8 * i) & 0xFF) for i in range(4)])
    
    print(f"Actual Settings: {actual_w}x{actual_h}, Codec: {codec}")
    
    print("Reading first 10 frames...")
    for i in range(10):
        t0 = time.time()
        ret, frame = cap.read()
        dt = (time.time() - t0) * 1000
        if not ret:
            print(f"Frame {i}: FAILED TO READ")
        else:
            print(f"Frame {i}: OK ({frame.shape}, {dt:.1f}ms)")
            
    cap.release()
    return True

if __name__ == "__main__":
    print(f"Platform: {platform.system()}")
    print(f"OpenCV Version: {cv2.__version__}")
    
    # Test DSHOW (Legacy, often stable)
    test_camera("CAP_DSHOW", cv2.CAP_DSHOW)
    
    # Test MSMF (Modern Windows Media Foundation)
    test_camera("CAP_MSMF", cv2.CAP_MSMF)
    
    # Test default
    test_camera("DEFAULT", 0)
