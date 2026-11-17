package cn.pump.control;

import java.math.BigDecimal;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** Shared, Android-independent protocol code. Wire positions are milli-revolutions. */
public final class Protocol {
    private Protocol() {}
    public static byte[] bytes(String command) {
        if (!command.matches("[A-Z0-9 .]+") || command.length() > 19)
            throw new IllegalArgumentException("指令格式错误");
        return (command + "\n").getBytes(StandardCharsets.US_ASCII);
    }
    public static String step(String text) {
        try {
            BigDecimal n = new BigDecimal(text.trim());
            if (!text.trim().matches("[0-9]+(\\.[0-9]{1,3})?") ||
                    n.compareTo(new BigDecimal("0.1")) < 0 || n.compareTo(BigDecimal.TEN) > 0)
                throw new IllegalArgumentException();
            return "STEP " + n.stripTrailingZeros().toPlainString();
        } catch (RuntimeException e) { throw new IllegalArgumentException("圈数应为 0.1～10，最多三位小数"); }
    }
    public static String speed(String text) {
        try {
            int value = Integer.parseInt(text.trim());
            if (value < 60 || value > 200) throw new IllegalArgumentException();
            return "SPEED " + value;
        } catch (RuntimeException e) { throw new IllegalArgumentException("速度应为 60～200 RPM 的整数"); }
    }
    public static int seconds(String text) {
        if (text.trim().isEmpty()) return 0;
        try {
            int value = Integer.parseInt(text.trim());
            if (value < 0 || value > 3600) throw new IllegalArgumentException();
            return value;
        } catch (RuntimeException e) { throw new IllegalArgumentException("抽水时长应为 0～3600 秒；0 表示手动停止"); }
    }
    public static String expected(String command) {
        if (command.equals("APP")) return "OK APP V1";
        if (command.equals("PING")) return "OK PONG";
        if (command.equals("STATUS")) return "STATE ";
        if (command.startsWith("STEP ")) return "OK STEP";
        if (command.startsWith("SPEED ")) return "OK SPEED";
        return "OK " + command;
    }
    public static boolean matches(String command, String reply) {
        return command.equals("STATUS") ? reply.startsWith("STATE ") : reply.equals(expected(command));
    }
    public static final class Decoder {
        private final StringBuilder line = new StringBuilder();
        public void clear() { line.setLength(0); }
        public List<String> feed(byte[] bytes) {
            List<String> lines = new ArrayList<>();
            for (byte raw : bytes) {
                int c = raw & 255;
                if (c == 10) {
                    String value = line.toString().trim(); line.setLength(0);
                    // Firmware may finish a binary VOFA frame just before APP acknowledgement.
                    int handshake = value.indexOf("OK APP V1");
                    if (handshake >= 0) value = value.substring(handshake);
                    if (value.startsWith("OK ") || value.startsWith("ERR ") ||
                            value.startsWith("STATE ") || value.startsWith("DATA ")) lines.add(value);
                } else if (c >= 32 && c <= 126) {
                    if (line.length() >= 2048) line.setLength(0);
                    line.append((char)c);
                }
            }
            return lines;
        }
    }
    public static final class State {
        public final int axis, rpm, pwm, pump, speed;
        public final long position, target, step;
        private State(Map<String, Long> values) {
            axis = values.get("AXIS").intValue(); rpm = values.get("RPM").intValue();
            pwm = values.get("PWM").intValue(); pump = values.get("PUMP").intValue();
            speed = values.get("SPEED").intValue(); position = values.get("POS");
            target = values.get("TARGET"); step = values.get("STEP");
            if (axis < 0 || axis > 6 || pump < 0 || pump > 1000 || speed < 60 || speed > 200 ||
                    step < 100 || step > 10000 || Math.abs((long)pwm) > 1000)
                throw new IllegalArgumentException("状态字段超出范围");
        }
        public static State parse(String text) {
            if (!text.startsWith("DATA ") && !text.startsWith("STATE "))
                throw new IllegalArgumentException("非状态消息");
            Map<String, Long> values = new HashMap<>();
            for (String token : text.split(" +")) {
                String[] parts = token.split("=", 2);
                if (parts.length == 2) values.put(parts[0], Long.parseLong(parts[1]));
            }
            for (String key : new String[]{"AXIS","POS","TARGET","RPM","PWM","PUMP","STEP","SPEED"})
                if (!values.containsKey(key)) throw new IllegalArgumentException("状态不完整");
            return new State(values);
        }
        public String label() {
            return new String[]{"已停止", "转轴运行中", "已到位", "运动超时", "反馈方向异常", "位置跟踪异常", "限位异常"}[axis];
        }
    }
}
