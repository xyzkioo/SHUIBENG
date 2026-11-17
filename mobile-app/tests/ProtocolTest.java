package cn.pump.control;
import java.nio.charset.StandardCharsets;
import java.util.List;

public class ProtocolTest {
    static int assertions;
    static void check(boolean condition) { assertions++; if(!condition) throw new AssertionError("check "+assertions); }
    static void invalid(Runnable action) {
        try { action.run(); } catch(IllegalArgumentException expected) { assertions++; return; }
        throw new AssertionError("invalid input accepted");
    }
    public static void main(String[] args) {
        check(Protocol.step("5.000").equals("STEP 5"));
        check(Protocol.step("0.125").equals("STEP 0.125"));
        check(Protocol.step("10").equals("STEP 10"));
        for(String s:new String[]{"0", "0.09", "11", "NaN", "1e0", "-1", "1.0000", "5\nSTOP"})invalid(()->Protocol.step(s));
        check(Protocol.speed("200").equals("SPEED 200"));
        for(String s:new String[]{"59", "201", "100.5", "abc"})invalid(()->Protocol.speed(s));
        check(Protocol.seconds("")==0&&Protocol.seconds("3600")==3600);
        invalid(()->Protocol.seconds("3601"));invalid(()->Protocol.seconds("-1"));
        byte[] command=Protocol.bytes("EXTEND");check(command.length==7&&command[6]==10);
        invalid(()->Protocol.bytes("EXTEND\nEXTEND"));
        Protocol.Decoder decoder=new Protocol.Decoder();
        check(decoder.feed(new byte[]{0,0,(byte)0xfa,0x43,0,0,(byte)0x80,0x7f,13,10}).isEmpty());
        check(decoder.feed("OK AP".getBytes(StandardCharsets.US_ASCII)).isEmpty());
        List<String> lines=decoder.feed("P V1\r\nOK STEP\nERR BUSY\r\n".getBytes(StandardCharsets.US_ASCII));
        check(lines.size()==3&&lines.get(0).equals("OK APP V1"));
        check(Protocol.matches("APP",lines.get(0)));check(!Protocol.matches("EXTEND","OK EXTEND AGAIN"));
        check(!Protocol.matches("STEP 5","OK SPEED"));
        String status="STATE AXIS=1 POS=-2500 TARGET=-5000 RPM=-180 PWM=-130 PUMP=800 STEP=5000 SPEED=200";
        for(byte b:(status+"\n").getBytes(StandardCharsets.US_ASCII)) {
            List<String> output=decoder.feed(new byte[]{b});
            if(!output.isEmpty()) { check(output.size()==1); Protocol.State state=Protocol.State.parse(output.get(0));
                check(state.position==-2500&&state.target==-5000&&state.rpm==-180&&state.pump==800&&state.step==5000); }
        }
        check(Protocol.State.parse(status.replace("AXIS=1","AXIS=2")).label().equals("已到位"));
        invalid(()->Protocol.State.parse("DATA AXIS=1"));
        invalid(()->Protocol.State.parse(status.replace("AXIS=1","AXIS=7")));
        invalid(()->Protocol.State.parse(status.replace("PUMP=800","PUMP=1001")));
        decoder.feed("DATA AXIS=".getBytes(StandardCharsets.US_ASCII));decoder.clear();
        check(decoder.feed("OK APP V1\n".getBytes(StandardCharsets.US_ASCII)).size()==1);
        System.out.println("PASS: "+assertions+" protocol assertions (validation, LF, framing, handshake, status, reconnect reset)");
    }
}
