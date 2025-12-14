## Related Repos (แยกตามหน้าที่)

โปรเจค Smart Humidifier แยกเป็น 3 repo เพื่อให้ดูแลง่าย:

1) **Repo หลัก (Gateway Node)** — *ที่คุณกำลังดูอยู่ตอนนี้*  
   - ESP32 Gateway: เชื่อม Wi‑Fi + Firebase RTDB  
   - อ่าน DHT11 (humidity/temp)  
   - ตัดสินใจโหมด **manual / auto / schedule** + safety  
   - ส่งคำสั่งไป Sensor Node ผ่าน **ESP‑NOW**

2) **Sensor Node + Mongo Trigger**  
   - Firmware ของ Sensor Node (water level + MPU6050 tilt + keypad + relay)  
   - Trigger/Listener ดึง RTDB ไปเก็บ MongoDB สำหรับ logs/history  
   👉 https://github.com/View-MG/humidifier-others

3) **Dashboard + WebSocket**  
   - Next.js Dashboard สำหรับดู/สั่งงาน  
   - `ws/` สำหรับ WebSocket server (เช่นรับเสียงไปทำ STT/AI)  
   👉 https://github.com/View-MG/humidifier-dashboard
