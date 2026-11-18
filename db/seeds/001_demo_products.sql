-- Fixed illustrative prices for development; not real-time retail quotations.
-- Re-running adds missing rows only, preserving edits to existing catalogue entries.
BEGIN;
INSERT INTO products(name,image_key,price_vnd) VALUES
('Gạo 5 kg','products/rice.png',150000),
('Sữa hộp','products/milk.png',35000),
('Bánh mì','products/bread.png',20000),
('Trứng 10 quả','products/eggs.png',40000),
('Táo 1 kg','products/apples.png',65000),
('Chuối 1 kg','products/bananas.png',25000),
('Cà phê 250 g','products/coffee.png',85000),
('Trà 100 g','products/tea.png',60000),
('Nước suối','products/water.png',10000),
('Nước cam','products/juice.png',30000),
('Mì gói','products/noodles.png',5000),
('Dầu ăn 1 lít','products/oil.png',55000),
('Nước mắm','products/fish_sauce.png',45000),
('Đường 1 kg','products/sugar.png',28000),
('Muối 500 g','products/salt.png',12000),
('Sữa chua','products/yogurt.png',8000),
('Bánh quy','products/biscuits.png',50000),
('Chocolate','products/chocolate.png',75000),
('Bút bi','products/pen.png',5000),
('Vở','products/notebook.png',15000),
('Balo','products/backpack.png',350000),
('Bình nước','products/bottle.png',120000),
('Tai nghe','products/headphones.png',500000),
('Bàn phím','products/keyboard.png',650000),
('Chuột máy tính','products/mouse.png',250000),
('Đèn bàn','products/lamp.png',300000),
('Quạt điện','products/fan.png',800000),
('Ấm siêu tốc','products/kettle.png',450000),
('Nồi cơm điện','products/rice_cooker.png',1200000),
('Máy xay sinh tố','products/blender.png',900000)
ON CONFLICT(image_key) DO NOTHING;
COMMIT;
