import cv2
import urllib.request
import numpy as np
from fer import FER

# Initialize the FER emotion detector
detector = FER(mtcnn=True)

# URL of the IP camera
url = 'http://172.16.20.159/cam-hi.jpg'

while True:
    # Capture frame-by-frame from the IP camera
    img_resp = urllib.request.urlopen(url)
    imgnp = np.array(bytearray(img_resp.read()), dtype=np.uint8)
    frame = cv2.imdecode(imgnp, -1)

    # Detect emotions in the frame
    result = detector.detect_emotions(frame)

    # Loop through detected faces and emotions
    for face in result:
        (x, y, w, h) = face["box"]
        emotions = face["emotions"]

        # Get the dominant emotion
        dominant_emotion = max(emotions, key=emotions.get)
        emotion_score = emotions[dominant_emotion]

        # Draw the bounding box and emotion label
        cv2.rectangle(frame, (x, y), (x + w, y + h), (255, 0, 0), 2)
        cv2.putText(frame, f'{dominant_emotion} {emotion_score:.2f}', (x, y - 10), 
                    cv2.FONT_HERSHEY_SIMPLEX, 0.9, (255, 0, 0), 2)

    # Display the resulting frame
    cv2.imshow('Emotion Detection', frame)

    # Break the loop on 'q' key press
    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

# Release the capture and close windows
cv2.destroyAllWindows()
