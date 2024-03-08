#!/bin/bash
set -e

mysql -v -h0 -P9306 -e "SHOW TABLES; DROP TABLE IF EXISTS t; SHOW TABLES; CREATE TABLE t (id BIGINT, model TEXT, storage_capacity INTEGER, color TEXT, release_year INTEGER, price FLOAT, sold BOOL, date_added TIMESTAMP, product_codes MULTI, values MULTI64, additional_info JSON) engine='columnar';"

total_iterations=10000

output_file="/tmp/insert_commands.sql"
echo "" > $output_file

for ((i=1; i<=total_iterations; i++)); do
    echo "INSERT INTO t (id, model, storage_capacity, color, release_year, price, sold, date_added, product_codes, values, additional_info) VALUES
	(${i}01, 'iPhone 13 Pro', 256, 'silver', 2021, 1099.99, 'TRUE', '1591362342000', (1,2,3), (523456764345678976, 98765409877866654098, 1109876543450987650987), '{\"features\": [\"ProMotion display\", \"A15 Bionic chip\", \"Ceramic Shield front cover\"]}'),
(${i}02, 'iPhone 13', 128, 'blue', 2021, 799.99, 'FALSE', '1625317932000', (4,5,6), (623456764345678976, 98765409877866654097, 1109876543450987650986), '{\"features\": [\"A15 Bionic chip\", \"Ceramic Shield front cover\", \"Dual-camera system\"]}'),
(${i}03, 'iPhone SE (2022)', 64, 'red', 2022, 399.99, 'TRUE', '1648044532000', (7,8,9), (723456764345678976, 98765409877866654096, 1109876543450987650985), '{\"features\": [\"A15 Bionic chip\", \"Touch ID\", \"4.7-inch Retina HD display\"]}'),
(${i}04, 'iPhone 12 Mini', 128, 'white', 2020, 699.99, 'FALSE', '1675080732000', (9,10,11), (823456764345678976, 98765409877866654095, 1109876543450987650984), '{\"features\": [\"A14 Bionic chip\", \"Super Retina XDR display\", \"Dual-camera system\"]}'),
(${i}05, 'iPhone 11', 64, 'black', 2019, 599.99, 'TRUE', '1721619286000', (12,13,14), (923456764345678976, 98765409877866654094, 1109876543450987650983), '{\"features\": [\"A13 Bionic chip\", \"Liquid Retina HD display\", \"Dual-camera system\"]}'),
(${i}06, 'iPhone X', 256, 'space gray', 2017, 999.99, 'TRUE', '1504682342000', (15,16,17), (1023456764345678976, 98765409877866654093, 1109876543450987650982), '{\"features\": [\"A11 Bionic chip\", \"Super Retina display\", \"Face ID\"]}'),
(${i}07, 'iPhone 8 Plus', 128, 'gold', 2017, 799.99, 'FALSE', '1493366342000', (18,19,20), (1123456764345678976, 98765409877866654092, 1109876543450987650981), '{\"features\": [\"A11 Bionic chip\", \"Retina HD display\", \"Dual-camera system\"]}'),
(${i}08, 'iPhone 7', 32, 'rose gold', 2016, 549.99, 'TRUE', '1478008742000', (21,22,23), (1223456764345678976, 98765409877866654091, 1109876543450987650980), '{\"features\": [\"A10 Fusion chip\", \"Retina HD display\", \"Water and dust resistance\"]}'),
(${i}09, 'iPhone SE (2016)', 16, 'silver', 2016, 399.99, 'TRUE', '1464363142000', (24,25,26), (1323456764345678976, 98765409877866654090, 1109876543450987650979), '{\"features\": [\"A9 chip\", \"Retina display\", \"Touch ID\"]}'),
(${i}10, 'iPhone 6s', 64, 'space gray', 2015, 449.99, 'FALSE', '1446538342000', (27,28,29), (1423456764345678976, 98765409877866654089, 1109876543450987650978), '{\"features\": [\"A9 chip\", \"Retina HD display\", \"3D Touch\"]}'),
(${i}11, 'Samsung Galaxy S21 Ultra', 512, 'phantom black', 2021, 1199.99, 'TRUE', '1631362342000', (30,31,32), (523456764345678976, 98765409877866654098, 1109876543450987650987), '{\"features\": [\"Dynamic AMOLED 2X\", \"Exynos 2100\", \"108MP camera\"]}'),
(${i}12, 'Google Pixel 6', 128, 'sorta seafoam', 2021, 699.99, 'TRUE', '1635317932000', (33,34,35), (623456764345678976, 98765409877866654097, 1109876543450987650986), '{\"features\": [\"Google Tensor chip\", \"Flat OLED display\", \"Dual-camera system\"]}'),
(${i}13, 'OnePlus 9', 256, 'winter mist', 2021, 729.99, 'FALSE', '1648044532000', (36,37,38), (723456764345678976, 98765409877866654096, 1109876543450987650985), '{\"features\": [\"Qualcomm Snapdragon 888\", \"Fluid AMOLED display\", \"Hasselblad Camera for Mobile\"]}'),
(${i}14, 'Xiaomi Mi 11 Ultra', 512, 'ceramic white', 2021, 1199.99, 'TRUE', '1655080732000', (39,40,41), (823456764345678976, 98765409877866654095, 1109876543450987650984), '{\"features\": [\"Qualcomm Snapdragon 888\", \"120Hz AMOLED display\", \"50MP camera\"]}'),
(${i}15, 'Sony Xperia 1 III', 256, 'frosted black', 2021, 1299.99, 'FALSE', '1661619286000', (42,43,44), (923456764345678976, 98765409877866654094, 1109876543450987650983), '{\"features\": [\"Snapdragon 888\", \"4K OLED display\", \"Triple-camera system\"]}'),
(${i}16, 'Huawei P40 Pro', 256, 'deep sea blue', 2020, 999.99, 'TRUE', '1668008742000', (45,46,47), (1023456764345678976, 98765409877866654093, 1109876543450987650982), '{\"features\": [\"Kirin 990 5G\", \"90Hz OLED display\", \"Quad-camera system\"]}'),
(${i}17, 'Motorola Edge 20 Pro', 256, 'midnight blue', 2021, 699.99, 'FALSE', '1676538342000', (48,49,50), (1123456764345678976, 98765409877866654092, 1109876543450987650981), '{\"features\": [\"Qualcomm Snapdragon 870\", \"OLED display\", \"Triple-camera system\"]}'),
(${i}18, 'Nokia G50', 128, 'midnight sun', 2021, 299.99, 'TRUE', '1685080732000', (51,52,53), (1223456764345678976, 98765409877866654091, 1109876543450987650980), '{\"features\": [\"Qualcomm Snapdragon 480 5G\", \"6.82-inch display\", \"Triple-camera system\"]}'),
(${i}19, 'BlackBerry Evolve X', 64, 'black', 2018, 599.99, 'FALSE', '1691619286000', (54,55,56), (1323456764345678976, 98765409877866654090, 1109876543450987650979), '{\"features\": [\"Qualcomm Snapdragon 660\", \"5.99-inch display\", \"Dual-camera system\"]}'),
(${i}20, 'LG Wing', 256, 'aurora gray', 2020, 999.99, 'TRUE', '1698008742000', (57,58,59), (1423456764345678976, 98765409877866654089, 1109876543450987650978), '{\"features\": [\"Qualcomm Snapdragon 765G\", \"Swivel display\", \"Triple-camera system\"]}'),
(${i}21, 'HTC U20 5G', 256, 'green', 2020, 499.99, 'FALSE', '1706538342000', (60,61,62), (1523456764345678976, 98765409877866654088, 1109876543450987650977), '{\"features\": [\"Qualcomm Snapdragon 765G\", \"6.8-inch display\", \"Quad-camera system\"]}'),
(${i}22, 'Motorola Moto G100', 256, 'blue', 2021, 499.99, 'TRUE', '1715080732000', (63,64,65), (1623456764345678976, 98765409877866654087, 1109876543450987650976), '{\"features\": [\"Snapdragon 870\", \"6.7-inch display\", \"Quad-camera system\"]}'),
(${i}23, 'Asus ROG Phone 5', 256, 'phantom black', 2021, 999.99, 'FALSE', '1721619286000', (66,67,68), (1723456764345678976, 98765409877866654086, 1109876543450987650975), '{\"features\": [\"Snapdragon 888\", \"AMOLED display\", \"Triple-camera system\"]}'),
(${i}24, 'Xiaomi Redmi Note 11 Pro', 128, 'glacier blue', 2021, 299.99, 'TRUE', '1728008742000', (69,70,71), (1823456764345678976, 98765409877866654085, 1109876543450987650974), '{\"features\": [\"Snapdragon 695\", \"6.67-inch display\", \"Quad-camera system\"]}'),
(${i}25, 'Sony Xperia 5 III', 256, 'black', 2021, 999.99, 'FALSE', '1736538342000', (72,73,74), (1923456764345678976, 98765409877866654084, 1109876543450987650973), '{\"features\": [\"Snapdragon 888\", \"6.1-inch display\", \"Triple-camera system\"]}'),
(${i}26, 'Google Pixel 5a 5G', 128, 'mostly black', 2021, 449.99, 'TRUE', '1745080732000', (75,76,77), (2023456764345678976, 98765409877866654083, 1109876543450987650972), '{\"features\": [\"Snapdragon 765G\", \"6.34-inch display\", \"Dual-camera system\"]}'),
(${i}27, 'BlackBerry Key2', 64, 'silver', 2018, 499.99, 'FALSE', '1745080732000', (75,76,77), (2023456764345678976, 98765409877866654083, 1109876543450987650972), '{\"features\": [\"Snapdragon 660\", \"4.5-inch display\", \"Physical keyboard\"]}'),
(${i}28, 'LG Velvet', 128, 'aurora silver', 2020, 599.99, 'TRUE', '1751619286000', (78,79,80), (2123456764345678976, 98765409877866654082, 1109876543450987650971), '{\"features\": [\"Snapdragon 765G\", \"6.8-inch display\", \"Triple-camera system\"]}'),
(${i}29, 'HTC Desire 21 Pro 5G', 128, 'purple', 2021, 499.99, 'FALSE', '1758008742000', (81,82,83), (2223456764345678976, 98765409877866654081, 1109876543450987650970), '{\"features\": [\"Snapdragon 690\", \"6.7-inch display\", \"Quad-camera system\"]}'),
(${i}30, 'Motorola Moto G Power (2021)', 64, 'flash gray', 2021, 249.99, 'TRUE', '1766538342000', (84,85,86), (2323456764345678976, 98765409877866654080, 1109876543450987650969), '{\"features\": [\"Snapdragon 662\", \"6.6-inch display\", \"Triple-camera system\"]}'),
(${i}31, 'Sony Xperia 10 III', 128, 'black', 2021, 399.99, 'FALSE', '1775080732000', (87,88,89), (2423456764345678976, 98765409877866654079, 1109876543450987650968), '{\"features\": [\"Snapdragon 690\", \"6.0-inch display\", \"Triple-camera system\"]}'),
(${i}32, 'Google Pixel 5', 128, 'just black', 2020, 699.99, 'TRUE', '1781619286000', (90,91,92), (2523456764345678976, 98765409877866654078, 1109876543450987650967), '{\"features\": [\"Snapdragon 765G\", \"6.0-inch display\", \"Dual-camera system\"]}'),
(${i}33, 'BlackBerry Motion', 32, 'black', 2017, 399.99, 'FALSE', '1788008742000', (93,94,95), (2623456764345678976, 98765409877866654077, 1109876543450987650966), '{\"features\": [\"Snapdragon 625\", \"5.5-inch display\", \"Water-resistant\"]}'),
(${i}34, 'Nokia 8.3 5G', 128, 'polar night', 2020, 699.99, 'TRUE', '1796538342000', (96,97,98), (2723456764345678976, 98765409877866654076, 1109876543450987650965), '{\"features\": [\"Snapdragon 765G\", \"6.81-inch display\", \"Quad-camera system\"]}'),
(${i}35, 'LG G8 ThinQ', 128, 'aurora black', 2019, 849.99, 'FALSE', '1805080732000', (99,100,101), (2823456764345678976, 98765409877866654075, 1109876543450987650964), '{\"features\": [\"Snapdragon 855\", \"6.1-inch display\", \"Dual-camera system\"]}'),
(${i}36, 'HTC Wildfire E1 lite', 32, 'black', 2021, 129.99, 'TRUE', '1811619286000', (102,103,104), (2923456764345678976, 98765409877866654074, 1109876543450987650963), '{\"features\": [\"MediaTek Helio A20\", \"6.1-inch display\", \"Dual-camera system\"]}'),
(${i}37, 'Motorola Moto G Stylus (2021)', 128, 'aurora black', 2021, 299.99, 'FALSE', '1818008742000', (105,106,107), (3023456764345678976, 98765409877866654073, 1109876543450987650962), '{\"features\": [\"Snapdragon 678\", \"6.8-inch display\", \"Quad-camera system\"]}'),
(${i}38, 'Sony Xperia 10 Plus', 64, 'black', 2019, 299.99, 'TRUE', '1826538342000', (108,109,110), (3123456764345678976, 98765409877866654072, 1109876543450987650961), '{\"features\": [\"Snapdragon 636\", \"6.5-inch display\", \"Dual-camera system\"]}'),
(${i}39, 'Google Pixel 4 XL', 64, 'just black', 2019, 899.99, 'FALSE', '1835080732000', (111,112,113), (3223456764345678976, 98765409877866654071, 1109876543450987650960), '{\"features\": [\"Snapdragon 855\", \"6.3-inch display\", \"Dual-camera system\"]}'),
(${i}40, 'BlackBerry Key2 LE', 64, 'space blue', 2018, 399.99, 'TRUE', '1841619286000', (114,115,116), (3323456764345678976, 98765409877866654070, 1109876543450987650959), '{\"features\": [\"Snapdragon 636\", \"4.5-inch display\", \"Physical keyboard\"]}'),
(${i}41, 'LG G7 ThinQ', 64, 'platinum gray', 2018, 599.99, 'FALSE', '1848008742000', (117,118,119), (3423456764345678976, 98765409877866654069, 1109876543450987650958), '{\"features\": [\"Snapdragon 845\", \"6.1-inch display\", \"Dual-camera system\"]}'),
(${i}42, 'HTC Desire 12', 32, 'cool black', 2018, 199.99, 'TRUE', '1856538342000', (120,121,122), (3523456764345678976, 98765409877866654068, 1109876543450987650957), '{\"features\": [\"MediaTek MT6739\", \"5.5-inch display\", \"Single camera\"]}'),
(${i}43, 'Motorola Moto G6 Plus', 64, 'deep indigo', 2018, 299.99, 'FALSE', '1865080732000', (123,124,125), (3623456764345678976, 98765409877866654067, 1109876543450987650956), '{\"features\": [\"Snapdragon 630\", \"5.9-inch display\", \"Dual-camera system\"]}'),
(${i}44, 'Sony Xperia XZ3', 64, 'forest green', 2018, 899.99, 'TRUE', '1871619286000', (126,127,128), (3723456764345678976, 98765409877866654066, 1109876543450987650955), '{\"features\": [\"Snapdragon 845\", \"6.0-inch display\", \"Single camera\"]}'),
(${i}45, 'Google Pixel 3', 128, 'not pink', 2018, 799.99, 'FALSE', '1878008742000', (129,130,131), (3823456764345678976, 98765409877866654065, 1109876543450987650954), '{\"features\": [\"Snapdragon 845\", \"5.5-inch display\", \"Single camera\"]}'),
(${i}46, 'BlackBerry KEYone', 32, 'silver', 2017, 549.99, 'TRUE', '1886538342000', (132,133,134), (3923456764345678976, 98765409877866654064, 1109876543450987650953), '{\"features\": [\"Snapdragon 625\", \"4.5-inch display\", \"Physical keyboard\"]}'),
(${i}47, 'LG V30', 64, 'aurora black', 2017, 799.99, 'FALSE', '1895080732000', (135,136,137), (4023456764345678976, 98765409877866654063, 1109876543450987650952), '{\"features\": [\"Snapdragon 835\", \"6.0-inch display\", \"Dual-camera system\"]}'),
(${i}48, 'HTC U11', 64, 'solar red', 2017, 649.99, 'TRUE', '1901619286000', (138,139,140), (4123456764345678976, 98765409877866654062, 1109876543450987650951), '{\"features\": [\"Snapdragon 835\", \"5.5-inch display\", \"Single camera\"]}'),
(${i}49, 'Motorola Moto Z2 Force', 64, 'super black', 2017, 799.99, 'FALSE', '1908008742000', (141,142,143), (4223456764345678976, 98765409877866654061, 1109876543450987650950), '{\"features\": [\"Snapdragon 835\", \"5.5-inch display\", \"Dual-camera system\"]}'),
(${i}50, 'Sony Xperia XZ Premium', 64, 'luminous chrome', 2017, 699.99, 'TRUE', '1916538342000', (144,145,146), (4323456764345678976, 98765409877866654060, 1109876543450987650949), '{\"features\": [\"Snapdragon 835\", \"5.46-inch display\", \"Single camera\"]}');" >> $output_file

done

mysql -h0 -P9306 < $output_file

mysql -h0 -P9306 -e "FLUSH RAMCHUNK t; SHOW TABLES; SELECT COUNT(*) FROM t;"
