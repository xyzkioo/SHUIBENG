package cn.pump.control;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.bluetooth.*;
import android.bluetooth.le.*;
import android.content.*;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.*;
import android.text.InputType;
import android.view.*;
import android.widget.*;
import java.util.*;

/** Foreground-only BLE controller. No internet, automatic reconnect or command replay. */
@SuppressWarnings("deprecation")
public final class MainActivity extends Activity {
    private static final int BG=0xff101b27, CARD=0xff192838, TEXT=0xfff3f7fb,
            MUTED=0xff9aafc1, TEAL=0xff27cfb1, RED=0xffed656d;
    private static final UUID SERVICE=uuid("ffe0"), RX=uuid("ffe1"), TX=uuid("ffe2"), CCC=uuid("2902");
    private final Handler handler=new Handler(Looper.getMainLooper());
    private final Protocol.Decoder decoder=new Protocol.Decoder();
    private final ArrayDeque<Command> queue=new ArrayDeque<>();
    private final ArrayList<BluetoothDevice> devices=new ArrayList<>();
    private final ArrayList<String> deviceLabels=new ArrayList<>();
    private final ArrayDeque<String> logLines=new ArrayDeque<>();
    private BluetoothAdapter adapter;
    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic writer;
    private BluetoothLeScanner scanner;
    private Command pending;
    private Protocol.State state;
    private boolean ready, connecting, writeBusy, actionBusy, foreground, scanning;
    private long lastState, pumpDeadline;
    private String connectionText="设备未连接", deviceName="", lastEvent="打开设备电源，然后选择连接设备";
    private TextView connection, hint, axisValue, posValue, speedValue, pumpValue, logView, pumpCountdown;
    private EditText stepInput, speedInput, durationInput;
    private Button connectButton, disconnectButton, extendButton, retractButton, applyButton, pumpButton, axisStopButton, stopButton;
    private ArrayAdapter<String> scanAdapter;
    private AlertDialog scanDialog;
    private boolean configSynced;

    private static UUID uuid(String shortId) { return UUID.fromString("0000"+shortId+"-0000-1000-8000-00805f9b34fb"); }
    private int dp(float value) { return (int)(value*getResources().getDisplayMetrics().density+.5f); }
    private GradientDrawable shape(int color,int radius) {
        GradientDrawable d=new GradientDrawable(); d.setColor(color); d.setCornerRadius(dp(radius)); return d;
    }
    private TextView text(String value,int sp,int color) {
        TextView t=new TextView(this); t.setText(value); t.setTextSize(sp); t.setTextColor(color); t.setPadding(0,dp(4),0,dp(4)); return t;
    }
    private LinearLayout vertical() { LinearLayout l=new LinearLayout(this); l.setOrientation(LinearLayout.VERTICAL); return l; }
    private LinearLayout row() { LinearLayout l=new LinearLayout(this); l.setOrientation(LinearLayout.HORIZONTAL); l.setGravity(Gravity.CENTER_VERTICAL); return l; }
    private void gap(LinearLayout l,int height) { Space s=new Space(this); l.addView(s,new LinearLayout.LayoutParams(1,dp(height))); }
    private Button button(String title,int color,Runnable action) {
        Button b=new Button(this); b.setText(title); b.setAllCaps(false); b.setTextSize(16);
        b.setTextColor(color==TEAL?BG:TEXT); b.setBackground(shape(color,13)); b.setMinHeight(dp(52));
        b.setOnClickListener(v->action.run()); return b;
    }
    private void addPair(LinearLayout parent,View a,View b) {
        LinearLayout r=row(); LinearLayout.LayoutParams left=new LinearLayout.LayoutParams(0,dp(52),1); left.setMargins(0,0,dp(6),0);
        LinearLayout.LayoutParams right=new LinearLayout.LayoutParams(0,dp(52),1); right.setMargins(dp(6),0,0,0);
        r.addView(a,left); r.addView(b,right); parent.addView(r);
    }
    private LinearLayout card(LinearLayout parent,String title,String subtitle) {
        LinearLayout c=vertical(); c.setPadding(dp(18),dp(14),dp(18),dp(18)); c.setBackground(shape(CARD,19));
        TextView header=text(title,19,TEXT); header.setTypeface(null,Typeface.BOLD); c.addView(header);
        if(subtitle!=null)c.addView(text(subtitle,12,MUTED)); gap(c,10);
        parent.addView(c,new LinearLayout.LayoutParams(-1,-2)); gap(parent,14); return c;
    }
    private EditText input(String value,boolean decimal) {
        EditText e=new EditText(this); e.setText(value); e.setTextSize(20); e.setTextColor(TEXT); e.setSingleLine(true);
        e.setSelectAllOnFocus(true); e.setInputType(InputType.TYPE_CLASS_NUMBER|(decimal?InputType.TYPE_NUMBER_FLAG_DECIMAL:0));
        e.setPadding(dp(12),dp(8),dp(12),dp(8)); e.setBackground(shape(0xff101e2c,10)); return e;
    }
    private LinearLayout field(String label,EditText input) {
        LinearLayout c=vertical(); c.addView(text(label,12,MUTED)); c.addView(input,new LinearLayout.LayoutParams(-1,dp(49))); return c;
    }
    private TextView metric(LinearLayout row,String label) {
        LinearLayout c=vertical(); c.addView(text(label,12,MUTED)); TextView value=text("—",20,TEXT);
        value.setTypeface(null,Typeface.BOLD); c.addView(value); row.addView(c,new LinearLayout.LayoutParams(0,-2,1)); return value;
    }
    @Override public void onCreate(Bundle bundle) {
        super.onCreate(bundle);
        getWindow().setStatusBarColor(BG); getWindow().setNavigationBarColor(BG);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        LinearLayout root=vertical(); root.setBackgroundColor(BG);
        root.setOnApplyWindowInsetsListener((view,insets)->{
            view.setPadding(dp(18)+insets.getSystemWindowInsetLeft(),dp(8)+insets.getSystemWindowInsetTop(),
                    dp(18)+insets.getSystemWindowInsetRight(),insets.getSystemWindowInsetBottom()); return insets;
        });
        TextView kicker=text("PUMP CONTROL",11,TEAL); kicker.setLetterSpacing(.18f); root.addView(kicker);
        TextView title=text("水泵控制",28,TEXT); title.setTypeface(null,Typeface.BOLD); root.addView(title);
        root.addView(text("转轴定位 · 水泵启停",13,MUTED)); gap(root,8);
        ScrollView scroll=new ScrollView(this); scroll.setFillViewport(true); LinearLayout content=vertical();
        scroll.addView(content); root.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        LinearLayout link=card(content,"设备连接",null);
        connection=text(connectionText,17,TEAL); link.addView(connection);
        hint=text(lastEvent,12,MUTED); link.addView(hint); gap(link,12);
        connectButton=button("搜索并连接",TEAL,this::beginScan);
        disconnectButton=button("断开",0xff283d50,()->stopAndDisconnect("已断开连接")); addPair(link,connectButton,disconnectButton);
        LinearLayout axis=card(content,"转轴定位","按设定圈数移动；短行程可能达不到最高速度");
        stepInput=input("5",true); speedInput=input("200",false);
        LinearLayout fields=row(); LinearLayout.LayoutParams fl=new LinearLayout.LayoutParams(0,-2,1); fl.setMargins(0,0,dp(8),0);
        fields.addView(field("移动圈数 / 圈",stepInput),fl); fields.addView(field("最高速度 / RPM",speedInput),new LinearLayout.LayoutParams(0,-2,1)); axis.addView(fields); gap(axis,12);
        extendButton=button("↓  伸出",TEAL,()->move(false)); retractButton=button("↑  收回",0xff355a78,()->move(true)); addPair(axis,extendButton,retractButton); gap(axis,10);
        applyButton=button("保存参数",0xff283d50,()->configure(null)); axisStopButton=button("停止转轴",0xff283d50,()->priorityStop("AXIS STOP")); addPair(axis,applyButton,axisStopButton);
        LinearLayout pump=card(content,"水泵控制","定时结束自动停泵；填 0 则手动停止");
        durationInput=input("0",false); pump.addView(field("抽水时长 / 秒",durationInput)); gap(pump,12);
        pumpButton=button("开始抽水",TEAL,this::togglePump); pump.addView(pumpButton,new LinearLayout.LayoutParams(-1,dp(52)));
        pumpCountdown=text("水泵已停止",12,MUTED); pump.addView(pumpCountdown);
        LinearLayout status=card(content,"实时状态",null); LinearLayout r1=row();
        axisValue=metric(r1,"转轴"); posValue=metric(r1,"当前位置 / 圈"); status.addView(r1); gap(status,10);
        LinearLayout r2=row(); speedValue=metric(r2,"实际速度 / RPM"); pumpValue=metric(r2,"水泵"); status.addView(r2);
        TextView show=text("查看通信记录 ▾",12,MUTED); status.addView(show);
        logView=text("",11,MUTED); logView.setTypeface(Typeface.MONOSPACE); logView.setVisibility(View.GONE); status.addView(logView);
        show.setOnClickListener(v->logView.setVisibility(logView.getVisibility()==View.VISIBLE?View.GONE:View.VISIBLE));
        stopButton=button("■  全部停止",RED,()->priorityStop("STOP")); LinearLayout.LayoutParams stopLayout=new LinearLayout.LayoutParams(-1,dp(56));
        stopLayout.setMargins(0,dp(8),0,dp(8)); root.addView(stopButton,stopLayout);
        setContentView(root);
        BluetoothManager manager=(BluetoothManager)getSystemService(BLUETOOTH_SERVICE); adapter=manager==null?null:manager.getAdapter();
        render(); handler.post(healthTick);
    }
    private void log(String message) {
        logLines.addLast(message); while(logLines.size()>12)logLines.removeFirst();
        StringBuilder text=new StringBuilder(); for(String line:logLines)text.append(line).append('\n'); logView.setText(text.toString());
    }
    private void event(String message) { lastEvent=message; log(message); render(); }
    private void error(String message) { actionBusy=false; event(message); Toast.makeText(this,message,Toast.LENGTH_LONG).show(); }
    private void render() {
        if(connection==null)return;
        connection.setText(connectionText); hint.setText(lastEvent);
        boolean live=ready&&state!=null&&SystemClock.elapsedRealtime()-lastState<2500;
        boolean idle=live&&state.axis!=1&&!actionBusy;
        connectButton.setEnabled(!connecting&&gatt==null); disconnectButton.setEnabled(gatt!=null);
        extendButton.setEnabled(idle); retractButton.setEnabled(idle); applyButton.setEnabled(idle);
        axisStopButton.setEnabled(ready&&!actionBusy); stopButton.setEnabled(gatt!=null); pumpButton.setEnabled(live&&!actionBusy);
        stepInput.setEnabled(idle); speedInput.setEnabled(idle); durationInput.setEnabled(live&&!actionBusy);
        for(Button b:new Button[]{connectButton,disconnectButton,extendButton,retractButton,applyButton,axisStopButton,stopButton,pumpButton}) b.setAlpha(b.isEnabled()?1:.4f);
        if(!live){axisValue.setText("—");posValue.setText("—");speedValue.setText("—");pumpValue.setText("—");}
        else {
            axisValue.setText(state.label()); axisValue.setTextColor(state.axis>=3?RED:TEXT);
            posValue.setText(String.format(Locale.US,"%.3f",state.position/1000.0));
            speedValue.setText(String.valueOf(Math.abs(state.rpm))); pumpValue.setText(state.pump==0?"已停止":"运行中");
        }
        boolean pumping=live&&state.pump>0; pumpButton.setText(pumping?"停止抽水":"开始抽水");
        pumpCountdown.setText(!pumping?"水泵已停止":pumpDeadline>0?"剩余 "+Math.max(0,(pumpDeadline-SystemClock.elapsedRealtime()+999)/1000)+" 秒":"手动模式 · 点击停止抽水结束");
    }
    private boolean permissions() {
        if(Build.VERSION.SDK_INT>=31)return checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN)==PackageManager.PERMISSION_GRANTED&&checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)==PackageManager.PERMISSION_GRANTED;
        return checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION)==PackageManager.PERMISSION_GRANTED;
    }
    private void beginScan() {
        if(adapter==null){error("这台手机不支持蓝牙");return;}
        if(!permissions()) {
            requestPermissions(Build.VERSION.SDK_INT>=31?new String[]{Manifest.permission.BLUETOOTH_SCAN,Manifest.permission.BLUETOOTH_CONNECT}:new String[]{Manifest.permission.ACCESS_FINE_LOCATION},7); return;
        }
        if(!adapter.isEnabled()){startActivityForResult(new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE),8);return;}
        stopScan(); devices.clear();deviceLabels.clear(); scanner=adapter.getBluetoothLeScanner();
        if(scanner==null){error("无法开始扫描，请检查手机蓝牙");return;}
        scanAdapter=new ArrayAdapter<>(this,android.R.layout.simple_list_item_1,deviceLabels);
        scanDialog=new AlertDialog.Builder(this).setTitle("选择蓝牙设备 · HC-05")
                .setAdapter(scanAdapter,(dialog,index)->connect(devices.get(index)))
                .setNegativeButton("取消",(dialog,which)->stopScan()).create();
        scanDialog.setOnDismissListener(dialog->stopScan()); scanDialog.show();
        scanning=true; scanner.startScan(null,new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(),scanCallback);
        event("正在搜索；请确认模块已上电，电脑已断开 BLE 连接");
        handler.removeCallbacks(scanTimeout);handler.postDelayed(scanTimeout,12000);
    }
    @Override public void onRequestPermissionsResult(int request,String[] names,int[] grants) {
        super.onRequestPermissionsResult(request,names,grants);
        if(request==7){if(permissions())beginScan();else error("需要附近设备权限才能连接蓝牙；安卓 11 及以前需要定位权限");}
    }
    @Override protected void onActivityResult(int request,int result,Intent data) {
        super.onActivityResult(request,result,data);if(request==8&&result==RESULT_OK)beginScan();
    }
    private final ScanCallback scanCallback=new ScanCallback(){
        @Override public void onScanResult(int type,ScanResult result){handler.post(()->{
            if(!scanning)return; BluetoothDevice d=result.getDevice(); if(devices.contains(d))return;
            String name=d.getName(); if(name==null&&result.getScanRecord()!=null)name=result.getScanRecord().getDeviceName();
            if(name==null)name="未命名设备";
            String label=name+"\n"+d.getAddress()+"  ·  "+result.getRssi()+" dBm";
            int index=name.toUpperCase(Locale.ROOT).contains("HC-05")?0:devices.size();
            devices.add(index,d);deviceLabels.add(index,label);if(scanAdapter!=null)scanAdapter.notifyDataSetChanged();
        });}
        @Override public void onScanFailed(int code){handler.post(()->{stopScan();error("扫描失败（"+code+"），稍后再试");});}
    };
    private final Runnable scanTimeout=()->{if(scanning){stopScan();event(devices.isEmpty()?"未发现设备；旧版安卓还需开启系统定位开关":"扫描完成，请选择设备");}};
    private void stopScan(){handler.removeCallbacks(scanTimeout);if(scanning&&scanner!=null&&permissions())try{scanner.stopScan(scanCallback);}catch(RuntimeException ignored){} scanning=false;}
    private void connect(BluetoothDevice device) {
        stopScan();closeConnection();connecting=true; deviceName=device.getName();if(deviceName==null)deviceName="蓝牙设备";
        connectionText="正在连接 "+deviceName;event("连接中，请稍候");
        gatt=device.connectGatt(this,false,callback,BluetoothDevice.TRANSPORT_LE);
        handler.postDelayed(connectTimeout,15000);
    }
    private final Runnable connectTimeout=()->{if(connecting)failConnection("连接超时，请确认模块未被电脑或其他手机占用");};
    private void failConnection(String message){closeConnection();connectionText="设备未连接";error(message);}
    private void closeConnection() {
        stopScan(); handler.removeCallbacks(connectTimeout);handler.removeCallbacks(commandTimeout);handler.removeCallbacks(pumpOff);
        ready=connecting=writeBusy=actionBusy=configSynced=false;pending=null;queue.clear();writer=null;state=null;pumpDeadline=0;lastState=0;decoder.clear();
        BluetoothGatt old=gatt;gatt=null;
        if(old!=null)try{old.disconnect();old.close();}catch(RuntimeException ignored){}
        render();
    }
    private void stopAndDisconnect(String message){
        if(gatt==null)return;priorityStop("STOP");handler.postDelayed(()->{closeConnection();connectionText="设备未连接";event(message);},300);
    }
    private final BluetoothGattCallback callback=new BluetoothGattCallback(){
        @Override public void onConnectionStateChange(BluetoothGatt source,int result,int newState){handler.post(()->{
            if(source!=gatt){source.close();return;}
            if(result!=BluetoothGatt.GATT_SUCCESS){failConnection("蓝牙连接失败（"+result+"），请重新连接");return;}
            if(newState==BluetoothProfile.STATE_CONNECTED){connectionText="正在初始化 "+deviceName;event("正在发现服务");if(!source.discoverServices())failConnection("无法发现蓝牙服务");}
            else if(newState==BluetoothProfile.STATE_DISCONNECTED)failConnection("连接已断开；设备会在心跳超时后停止");
        });}
        @Override public void onServicesDiscovered(BluetoothGatt source,int result){handler.post(()->{
            if(source!=gatt)return;
            BluetoothGattService service=source.getService(SERVICE);
            if(result!=BluetoothGatt.GATT_SUCCESS||service==null){failConnection("设备未提供匹配的控制服务，请选择 HC-05");return;}
            BluetoothGattCharacteristic notify=service.getCharacteristic(RX);
            writer=service.getCharacteristic(TX);if(!writable(writer))writer=service.getCharacteristic(RX);
            if(notify==null||!writable(writer)){failConnection("设备缺少数据收发通道");return;}
            BluetoothGattDescriptor descriptor=notify.getDescriptor(CCC);
            if(descriptor==null||!source.setCharacteristicNotification(notify,true)){failConnection("设备无法开启状态通知");return;}
            byte[] value=(notify.getProperties()&BluetoothGattCharacteristic.PROPERTY_NOTIFY)!=0?BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE:BluetoothGattDescriptor.ENABLE_INDICATION_VALUE;
            boolean accepted;
            if(Build.VERSION.SDK_INT>=33)accepted=source.writeDescriptor(descriptor,value)==0;
            else{descriptor.setValue(value);accepted=source.writeDescriptor(descriptor);}
            if(!accepted)failConnection("无法订阅设备状态");
        });}
        @Override public void onDescriptorWrite(BluetoothGatt source,BluetoothGattDescriptor descriptor,int result){handler.post(()->{
            if(source!=gatt)return;
            if(result!=BluetoothGatt.GATT_SUCCESS){failConnection("订阅状态失败（"+result+"）");return;}
            send("APP",()->{ready=true;connecting=false;handler.removeCallbacks(connectTimeout);connectionText="已连接 · "+deviceName;event("连接成功，正在同步状态");send("STATUS",null);});
        });}
        @Override public void onCharacteristicWrite(BluetoothGatt source,BluetoothGattCharacteristic characteristic,int result){handler.post(()->{
            if(source!=gatt)return;writeBusy=false;
            if(result!=BluetoothGatt.GATT_SUCCESS){failConnection("发送失败（"+result+"），指令未自动重发");return;}
            finishOrPump();
        });}
        @Override public void onCharacteristicChanged(BluetoothGatt source,BluetoothGattCharacteristic characteristic){
            byte[] value=characteristic.getValue();if(value!=null)received(source,value.clone());
        }
        @Override public void onCharacteristicChanged(BluetoothGatt source,BluetoothGattCharacteristic characteristic,byte[] value){received(source,value.clone());}
    };
    private boolean writable(BluetoothGattCharacteristic c){return c!=null&&(c.getProperties()&(BluetoothGattCharacteristic.PROPERTY_WRITE|BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE))!=0;}
    private void received(BluetoothGatt source,byte[] bytes){handler.post(()->{
        if(source!=gatt)return;
        for(String line:decoder.feed(bytes)){
            if(line.startsWith("STATE ")||line.startsWith("DATA ")){
                try{
                    Protocol.State update=Protocol.State.parse(line);int previous=state==null?-1:state.axis;
                    state=update;lastState=SystemClock.elapsedRealtime();
                    if(!configSynced){stepInput.setText(new java.math.BigDecimal(state.step).movePointLeft(3).stripTrailingZeros().toPlainString());speedInput.setText(String.valueOf(state.speed));configSynced=true;}
                    if(state.pump==0&&pumpDeadline!=0){pumpDeadline=0;handler.removeCallbacks(pumpOff);}
                    if(previous!=state.axis){if(state.axis>=3){event("设备已停止："+state.label());}else if(state.axis==2)event("转轴已到位");}
                    render();
                }catch(RuntimeException ignored){log("忽略不完整状态");}
            }
            if(line.startsWith("ERR ")){
                log("← "+line);
                handler.removeCallbacks(commandTimeout);pending=null;queue.clear();actionBusy=false;
                error(friendlyError(line));
                if(!ready){failConnection("设备握手失败，请确认已烧录手机控制固件");return;}
                if(!writeBusy)pumpQueue();
            }else if(pending!=null&&Protocol.matches(pending.command,line)){
                pending.acknowledged=true; if(!pending.command.equals("PING"))log("← "+line);finishOrPump();
            }
        }
    });}
    private String friendlyError(String text){
        if(text.contains("HEARTBEAT"))return "心跳中断，设备已停止";
        if(text.contains("BUSY"))return "设备忙，请等待转轴停止后再操作";
        if(text.contains("RANGE"))return "参数超出设备允许范围";
        return "设备回复："+text;
    }
    private static final class Command {
        final String command,expected;final Runnable success;boolean acknowledged;
        Command(String command,Runnable success){this.command=command;this.expected=Protocol.expected(command);this.success=success;}
    }
    private void send(String command,Runnable success){
        if(gatt==null||writer==null)return;
        if(queue.size()>12){error("操作过于频繁，请稍候");return;}
        queue.addLast(new Command(command,success));pumpQueue();
    }
    private void pumpQueue(){
        if(gatt==null||writer==null||writeBusy||pending!=null||queue.isEmpty())return;
        pending=queue.removeFirst();byte[] payload=Protocol.bytes(pending.command);
        int type=(writer.getProperties()&BluetoothGattCharacteristic.PROPERTY_WRITE)!=0?BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT:BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE;
        writeBusy=true;boolean accepted;
        if(Build.VERSION.SDK_INT>=33)accepted=gatt.writeCharacteristic(writer,payload,type)==0;
        else{writer.setWriteType(type);writer.setValue(payload);accepted=gatt.writeCharacteristic(writer);}
        if(!accepted){writeBusy=false;failConnection("蓝牙发送未被接受，请重新连接；动作未重试");return;}
        if(!pending.command.equals("PING"))log("→ "+pending.command);
        handler.removeCallbacks(commandTimeout);handler.postDelayed(commandTimeout,2500);
    }
    private final Runnable commandTimeout=()->{
        if(pending!=null){String name=pending.command;priorityStop("STOP");handler.postDelayed(()->failConnection("指令回复超时："+name+"；已尝试停止，请重新连接确认"),200);}
    };
    private void finishOrPump(){
        if(writeBusy)return;
        if(pending!=null&&pending.acknowledged){Command done=pending;pending=null;handler.removeCallbacks(commandTimeout);if(done.success!=null)done.success.run();}
        if(pending==null)pumpQueue();
    }
    private void priorityStop(String command){
        if(command.equals("STOP")){pumpDeadline=0;handler.removeCallbacks(pumpOff);}
        handler.removeCallbacks(commandTimeout);
        queue.clear();pending=null;actionBusy=false;
        if(gatt!=null&&writer!=null){queue.addFirst(new Command(command,()->event(command.equals("STOP")?"已请求全部停止":"已请求停止转轴")));pumpQueue();}
    }
    private void configure(Runnable after){
        if(!ready||state==null||state.axis==1||actionBusy)return;
        final String step,speed;
        try{step=Protocol.step(stepInput.getText().toString());speed=Protocol.speed(speedInput.getText().toString());}
        catch(IllegalArgumentException e){error(e.getMessage());return;}
        actionBusy=true;render();
        send(step,()->send(speed,()->{if(after==null){actionBusy=false;event("参数已保存");send("STATUS",null);}else after.run();}));
    }
    private void move(boolean retract){configure(()->send(retract?"RETRACT":"EXTEND",()->{actionBusy=false;event(retract?"开始收回":"开始伸出");render();}));}
    private void togglePump(){
        if(!ready||state==null)return;
        if(state.pump>0){pumpDeadline=0;handler.removeCallbacks(pumpOff);send("PUMP OFF",()->event("已请求停止抽水"));return;}
        final int seconds;try{seconds=Protocol.seconds(durationInput.getText().toString());}catch(IllegalArgumentException e){error(e.getMessage());return;}
        actionBusy=true;render();send("PUMP ON",()->{
            actionBusy=false;pumpDeadline=seconds>0?SystemClock.elapsedRealtime()+seconds*1000L:0;
            handler.removeCallbacks(pumpOff);if(seconds>0)handler.postDelayed(pumpOff,seconds*1000L);
            event(seconds>0?"开始定时抽水":"开始抽水");
        });
    }
    private final Runnable pumpOff=()->{pumpDeadline=0;if(ready)send("PUMP OFF",()->event("定时结束，已请求停泵"));};
    private final Runnable healthTick=new Runnable(){@Override public void run(){
        if(foreground&&ready){
            if(lastState>0&&SystemClock.elapsedRealtime()-lastState>3500){stopAndDisconnect("状态更新中断，已尝试停止；请重新连接确认");}
            else if(pending==null&&queue.isEmpty())send("PING",null);
        }
        render();handler.postDelayed(this,1000);
    }};
    @Override protected void onResume(){super.onResume();foreground=true;}
    @Override protected void onStop(){foreground=false;stopScan();if(gatt!=null)stopAndDisconnect("已请求停止并断开；回到页面后请重新连接");super.onStop();}
    @Override protected void onDestroy(){handler.removeCallbacksAndMessages(null);closeConnection();super.onDestroy();}
}
