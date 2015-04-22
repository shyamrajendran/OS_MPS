package DLB;
import DLB.Utils.*;

import java.io.IOException;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingDeque;

/**
 * Created by saikat on 4/21/15.
 */
public class UIThread extends Thread {
    private BlockingQueue<Message> messages;
    static Map<Integer, StateInfo> machineStateMap;

    UIThread() {
        System.out.println("UI thread starting on local node");
        messages = new LinkedBlockingDeque<Message>();
        machineStateMap = new HashMap<Integer, StateInfo>();
    }

    protected synchronized void addMessage(Message msg) {
        messages.add(msg);
    }

    private void getStates() throws IOException, InterruptedException {
        Message incomingMsg = messages.take();
        switch (incomingMsg.getMsgType()) {
            case HW:
                int machineId = incomingMsg.getMachineId();
                StateInfo machineState = (StateInfo) incomingMsg.getData();
                System.out.println("UI thread got msg from machine:" + machineId);
                machineStateMap.put(machineId, machineState);
                break;
            default:
                System.out.println("unknown msg to ui thread");
                break;
        }
    }

    @Override
    public void run() {
        super.run();
        while (!MainThread.STOP_SIGNAL) {
            try {
                getStates();
            } catch (InterruptedException e) {
                //e.printStackTrace();
                System.out.println("Cannot continue w/o connection");
                MainThread.stop();
            } catch (IOException e) {
                //e.printStackTrace();
                System.out.println("Cannot continue w/o connection");
                MainThread.stop();
            }
        }

    }
}
