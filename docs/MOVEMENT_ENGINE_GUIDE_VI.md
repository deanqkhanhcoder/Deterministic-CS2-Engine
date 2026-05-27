# Hướng Dẫn Sử Dụng Movement Engine (CS2)

Tài liệu này giải thích cách hoạt động của Movement Engine và cách tinh chỉnh các thông số để đạt hiệu quả cao nhất trong CS2.

---

## 1. Cơ Bản Về Counter-Strafe

**Counter-strafing là gì?**
Trong CS2, khi bạn đang di chuyển (ví dụ: giữ phím `A` để đi sang trái) và muốn dừng lại ngay lập tức để bắn chính xác, việc thả phím `A` ra là chưa đủ. Nhân vật của bạn sẽ bị trượt đi một quãng ngắn do quán tính. 
Để dừng lại ngay lập tức, bạn phải gõ nhanh phím ngược lại (phím `D`). Kỹ thuật này gọi là Counter-Strafe.

**Tại sao thời gian dừng lại quan trọng?**
Hệ thống Sub-tick của CS2 ghi nhận các hành động (di chuyển, bắn) chính xác đến từng micro-giây. Nếu bạn bắn trước khi nhân vật dừng hẳn, viên đạn sẽ bay lệch. Nếu bạn chờ quá lâu sau khi dừng, đối thủ sẽ có lợi thế. Movement Engine giúp bạn tự động hóa và tối ưu hóa quá trình dừng này đạt đến độ hoàn hảo của sub-tick.

---

## 2. Micro-Overlap (Thời Gian Gối Phím)

**Overlap là gì?**
Khi bạn đổi hướng (ví dụ: thả `A` và bấm `D`), có một khoảng thời gian cực ngắn mà cả hai phím đều được ghi nhận là đang bấm cùng lúc.

Quá trình diễn ra như sau:
1. Đang giữ `A`
2. Bấm `D` (Bắt đầu Overlap: Cả `A` và `D` đều kích hoạt)
3. Thả `A` (Kết thúc Overlap)
4. Giữ `D` để phanh (Brake)
5. Thả `D` (Dừng hẳn)

**Tại sao Overlap quan trọng?**
- Nếu **có Overlap**: Engine game CS2 sẽ kích hoạt phanh ngay lập tức và triệt tiêu gia tốc trượt.
- Nếu **Overlap quá cao**: Cảm giác di chuyển sẽ bị "lầy" (muddy), nhân vật có cảm giác nặng nề và khó đổi hướng liên tục.
- Nếu **Overlap quá thấp**: Game có thể không nhận diện được lệnh phanh, khiến bạn vẫn bị trượt đi.

**Khuyến nghị:**
- **Legit**: 12000 µs (12ms)
- **Aggressive**: 15000 µs (15ms)
- **Sniper**: 25000 µs (25ms)
- **SMG**: 8000 µs (8ms)

---

## 3. Brake Duration (Thời Gian Phanh)

**Brake Duration là gì?**
Sau khi quá trình Overlap kết thúc (bạn đã nhả phím cũ), hệ thống sẽ tiếp tục giữ phím ngược chiều trong một khoảng thời gian ngắn để hãm toàn bộ quán tính còn lại. Đây gọi là khoảng thời gian phanh.

**Sự đánh đổi:**
- **Phanh dài (Longer Brake)**: Dừng lại cực kì chắc chắn và ổn định. Tỷ lệ đạn bay chính xác ở viên đầu tiên là 100%. Tuy nhiên, nếu bạn muốn tiếp tục di chuyển ngay lập tức, nhân vật sẽ có cảm giác bị "khựng" lại.
- **Phanh ngắn (Shorter Brake)**: Cảm giác di chuyển lướt và phản hồi tốt hơn, phù hợp để jiggle-peek liên tục. Nhưng nếu phanh quá ngắn, bạn có thể chưa dừng hẳn khi bóp cò.

---

## 4. Debounce Compensation (Bù Trừ Độ Trễ Switch)

**Debounce là gì?**
Bàn phím cơ học truyền thống sử dụng lá đồng. Khi phím nảy lên, lá đồng sẽ rung nhẹ tạo ra nhiều tín hiệu giả (chattering). Để chống lại điều này, bàn phím áp dụng một khoảng trễ gọi là "Debounce Delay" (thường là 5ms - 15ms).

Nếu bạn dùng bàn phím nam châm (Hall Effect) hay quang học (Optical) có Rapid Trigger, độ trễ này gần như bằng 0.

**Tại sao cần bù trừ?**
Engine cần biết chính xác khi nào bạn thực sự nhả phím. Tính năng bù trừ Debounce sẽ tính toán trừ hao độ trễ của phần cứng, giúp thời điểm bắt đầu phanh chuẩn xác nhất với ý định của bạn. 
*Lưu ý:* Cài đặt thông số này thấp hơn độ trễ thực tế của bàn phím sẽ gây ra hiện tượng giật cục (jitter).

---

## 5. Humanization (Nhân Hóa Thời Gian)

**Tại sao không nên có timing hoàn hảo?**
Nếu Movement Engine luôn luôn thực hiện Overlap chính xác ở mức 12.000ms và Brake ở mức 15.000ms trong 10.000 lần liên tiếp, hệ thống Anti-Cheat trên server có thể dùng thuật toán thống kê để nhận diện đây là phần mềm tự động (anti-pattern).

**Giải pháp:**
Hệ thống **Humanization** sẽ cộng thêm một lượng thời gian ngẫu nhiên (nhiễu) vào các thông số trên mỗi lần thao tác, giúp mô phỏng sự không hoàn hảo của con người.

**Mức an toàn:**
Nên đặt khoảng dao động từ `500 µs` đến `3000 µs` (0.5ms - 3ms) để đảm bảo an toàn mà không làm hỏng trải nghiệm Counter-Strafe.

---

## 6. Weapon Profiles (Cấu Hình Vũ Khí)

Mỗi loại súng trong CS2 có một mức độ nặng nhẹ (max speed) và quán tính khác nhau:
- **Rifle (AK47, M4)**: Cần mức độ cân bằng. Dừng đủ chắc để tap, nhưng cũng đủ mượt để strafe.
- **Pistol (USP, Glock)**: Di chuyển rất nhanh và lướt. Cần phanh rất ngắn.
- **Sniper (AWP)**: Rất nặng. Cần Overlap và Brake cực dài để đảm bảo nhân vật dừng hoàn toàn tĩnh trước khi vẩy tâm.
- **SMG (Mac-10, MP9)**: Yêu cầu chạy và bắn liên tục (Run and Gun). Gần như không cần phanh nhiều.

---

## 7. Bảng Thông Số Khuyên Dùng (Presets)

Dưới đây là các cấu hình đã được kiểm thử độ ổn định và an toàn:

| Profile / Nút bấm | Khuyên Dùng Cho | Overlap (µs) | Brake (µs) | Humanize (µs) |
| :--- | :--- | :--- | :--- | :--- |
| **LEGIT** | Rifle (Bắn cẩn thận) | 12.000 | 15.000 | 1000 - 3000 |
| **AGGRESSIVE** | Rifle (Jiggle peek liên tục)| 15.000 | 20.000 | 500 - 1500 |
| **SNIPER** | AWP, Scout | 25.000 | 30.000 | 1000 - 3000 |
| **SMG** | MP9, Mac-10 | 8.000 | 10.000 | 500 - 1500 |

*(Bạn có thể áp dụng nhanh các thông số này bằng các nút bấm có sẵn ở cuối tab Settings).*

---

## 8. Kiến Thức Chuyên Sâu (Advanced)

Dành cho người muốn hiểu sâu về hệ thống:
- **Sub-Tick Compression**: Thay vì chờ đến tick tiếp theo (15.6ms cho server 64-tick), hệ thống sẽ nén các input lại và gửi timestamp chính xác về quá khứ cho server, giúp viên đạn của bạn luôn ở trạng thái "đã dừng".
- **Injection Drift & Scheduler Jitter**: Windows OS không phải là hệ điều hành thời gian thực. Sẽ có những lúc CPU bị bận, gây ra độ trễ (Jitter) khoảng 1-2ms. Hệ thống đã có cơ chế tự động theo dõi và bù trừ các Jitter này (hiển thị màu cam/đỏ trên đồ thị Timeline ở bản Debug/Profile). Bạn không cần lo lắng về điều này.

---

## 9. An Toàn và Ổn Định

**Những điều tuyệt đối tránh:**
1. **Overlap quá lớn (ví dụ > 50.000µs / 50ms):** Bạn sẽ cảm thấy nhân vật bị khựng cứng lại khi chuyển hướng, hoàn toàn mất khả năng né đạn.
2. **Brake quá dài (> 50ms):** Nhân vật sẽ tự động bước thêm một bước nhỏ theo hướng ngược lại, làm hỏng hoàn toàn aim của bạn.
3. **Debounce quá thấp so với bàn phím thường:** Sẽ gây hiện tượng nhân vật giật giật (stutter) tại chỗ do phím nảy lên nảy xuống nhiều lần. Nếu dùng phím cơ thường, hãy giữ Debounce ở mức 10000 - 15000. Nếu dùng phím nam châm (Wooting), có thể để 1000 - 5000.
