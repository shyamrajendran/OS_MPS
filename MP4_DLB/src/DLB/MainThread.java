package DLB;

import DLB.Utils.Job;
import DLB.Utils.Message;
import DLB.Utils.MessageType;
import DLB.Utils.SystemStat;
import org.hyperic.sigar.SigarException;

import java.io.IOException;
import java.lang.reflect.Field;
import java.net.*;
import java.util.Arrays;
import java.util.concurrent.BlockingDeque;
import java.util.concurrent.LinkedBlockingDeque;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;

/**
 * Created by manshu on 4/16/15.
 */
public class MainThread {
    protected static TransferManagerThread transferManagerThread;
    protected static StateManagerThread stateManagerThread;
    protected static AdapterThread adapterThread;
    protected static HWMonitorThread hwMonitorThread;
    protected static CommunicationThread[] communicationThread;
    protected static DynamicBalancerUI dynamicBalancerUI;

    protected volatile static boolean STOP_SIGNAL;
    protected volatile static double GUARD;

    protected static int numJobs = 1024;
    protected static int numWorkerThreads = 1;

    protected static int utilizationFactor = 1000;
    protected static int numElementsPrint = 10;
    protected static int collectionRate = 5; // in ms

    protected static int queueDifferenceThreshold = 20;
    protected static int cpuThresholdLimit = 10;

    protected static int numElements = 1024 * 1024 * 4;//1024 * 1024 * 32;

    protected static double initVal = 1.111111, addVal = 1.111111;
    protected static double[] vectorA;
    protected static double[] vectorB;

    protected static BlockingDeque<Job> jobQueue;
    protected static BlockingDeque<Job> resultantJobQueue;

    protected static ServerSocket serverSocket[];

    protected volatile static Socket mySocket[];

    protected volatile static int transferFlag;
    protected volatile static double timePerJob;


    protected static int machineId = 0;

    // locks for 2 volatile variables.
    protected static volatile Object jobInQueueLock = new Object();
    protected static volatile Object jobInComingLock = new Object();

    protected static volatile boolean jobsInQueue = false;
    protected static volatile boolean jobsInComing = false;

    protected static int elementsDone;
    protected static int localJobsDone;
    protected static int remoteJobsDone;
    protected static boolean processingDone;
    protected static int resultTransferred;
    protected static int finalRemoteJobs;

    protected static double throttlingValue = 0.5;
    protected static boolean isLocal = !true;
    protected static String ip = "localhost";//"172.17.116.149";//"jalatif2.ddns.net"; //"localhost";
    protected static int[] port = {2211, 2212, 2213};
    protected static enum TRANSFER_TYPE {
        DATA,
        STATE,
        RESULT
    }

    public MainThread() throws IOException, SigarException, IllegalAccessException,
                                                            NoSuchFieldException, InterruptedException {
//        System.out.println(getClass().getClassLoader().getResource(".").getPath());
//        String path = getClass().getClassLoader().getResource("DLB/res/lib").getPath();
//        System.out.println(path);
//        System.setProperty("java.library.path", path);
//        Field fieldSysPath = ClassLoader.class.getDeclaredField( "sys_paths" );
//        fieldSysPath.setAccessible( true );
//        fieldSysPath.set(null, null);

        transferManagerThread = new TransferManagerThread();
        stateManagerThread = new StateManagerThread();
        hwMonitorThread = new HWMonitorThread();
        adapterThread = new AdapterThread();
        communicationThread = new CommunicationThread[TRANSFER_TYPE.values().length];
        for (int i = 0; i < TRANSFER_TYPE.values().length; i++) {
            communicationThread[i] = new CommunicationThread(i);
        }
        if (isLocal)
            dynamicBalancerUI = new DynamicBalancerUI();
    }

    protected static synchronized void setLocalJobsDone(int jobs) {
        localJobsDone = jobs;
    }

    protected static synchronized void setRemoteJobsDone(int jobs) {
        remoteJobsDone = jobs;
    }

    public void start() throws InterruptedException {
        STOP_SIGNAL = false;
        processingDone = false;
        finalRemoteJobs = 0;

        if (!isLocal) machineId = 1;

        jobQueue = new LinkedBlockingDeque<Job>();
        if (!isLocal)
            resultantJobQueue = new LinkedBlockingDeque<Job>();

        if (isLocal) {
            vectorA = new double[numElements];
            Arrays.fill(vectorA, initVal);

            vectorB = new double[numElements];
            elementsDone = 0;
            remoteJobsDone = 0;
            localJobsDone = 0;
            resultTransferred = 0;
        }

        adapterThread.start();
        transferManagerThread.start();
        stateManagerThread.start();
        if (isLocal)
            dynamicBalancerUI.start();
    }

    protected static synchronized void addToResult(Job job) {
        Double[] data = job.getData();
        for (int i = job.getStartIndex(); i < job.getEndIndex(); i++) {
            vectorB[i] = data[i - job.getStartIndex()];
        }
        elementsDone += data.length;
        localJobsDone += 1;
        if (processingDone) {
            if (isLocal) {
                double progress = (resultTransferred) * 10000.0;
                progress = progress / (1.0 * finalRemoteJobs);
                //dynamicBalancerUI.setProgress((int) progress);
                dynamicBalancerUI.addMessage(new Message(MainThread.machineId, MessageType.ResultProgress, progress));
            }
        } else {
            int total_elements_done = elementsDone + (remoteJobsDone * (job.getEndIndex() - job.getStartIndex() + 1));
            if (isLocal) {
                double progress = (total_elements_done) * 10000.0;
                progress = progress / (1.0 * numElements);
                //dynamicBalancerUI.setProgress((int) progress);
                dynamicBalancerUI.addMessage(new Message(MainThread.machineId, MessageType.Progress, progress));
            }
            if (total_elements_done >= numElements) {
                processingDone = true;
                finalRemoteJobs = remoteJobsDone;
                System.out.println("Finished Computing everything");
                for (int i = 0; i < Math.min(numElements, numElementsPrint); i++)
                    System.out.print(vectorB[i] + " ");
                System.out.println("......");
                Message msg = new Message(machineId, MessageType.FinishACK, 0);
                try {
                    communicationThread[0].sendMessage(msg);
                } catch (IOException ie) {
                    stop();
                }
            }
        }
    }

    public static void stop() {
        STOP_SIGNAL = true;
        try {
            Thread.sleep(2000);
        } catch (InterruptedException e) {
            e.printStackTrace();
        }

        try {
            mySocket[0].close();
            mySocket[1].close();
            mySocket[2].close();
        } catch (IOException e) {
            e.printStackTrace();
        }
        /////////////////////Test Output//////////////////////////
        if (isLocal) {
            System.out.println("Wait testing the output");
            for (int i = 0; i < vectorB.length; i++) {
                if (vectorB[i] != (vectorA[i] + addVal)) {
                    System.out.println("Resultant Output incorrect at " + i + " index with value = " + vectorB[i]);
                    System.exit(1);
                }
            }
            System.out.println("Resultant Output is all correct");
        }
        ///////////////////////////////////////////////////////
        System.exit(0);
    }

    public void connect(String ip) throws IOException {
        mySocket = new Socket[TRANSFER_TYPE.values().length];
        if (!isLocal) {
            for (int i = 0; i < TRANSFER_TYPE.values().length; i++) {
                mySocket[i] = new Socket(ip, port[i]);
            }
        } else {

            InetAddress locIP = InetAddress.getByName(ip);

            System.out.println(Inet4Address.getLocalHost().getHostAddress());
            serverSocket = new ServerSocket[TRANSFER_TYPE.values().length];
            for (int i = 0; i < TRANSFER_TYPE.values().length; i++) {
                serverSocket[i] = new ServerSocket(port[i], 0, locIP);
                System.out.println("Listening on " + serverSocket[i]);
            }

            for (int i = 0; i < TRANSFER_TYPE.values().length; i++) {
                mySocket[i] = serverSocket[i].accept();
                System.out.println("Connection from " + mySocket[i]);
            }
        }

        for (int i = 0; i < TRANSFER_TYPE.values().length; i++) {
            communicationThread[i].setUpStreams();
            communicationThread[i].start();
        }

    }

    public void timePerJobCalc() {
        int elementsPerJob = MainThread.numElements / MainThread.numJobs;
        long timeTotal= 0 ;
        for (int i = 0; i < 10; i++) { // calculating 10 times average
            long t1 = System.currentTimeMillis();
            double[] data = new double[elementsPerJob];
            Arrays.fill(data, MainThread.initVal);
            for (int j = 0; j < elementsPerJob; j++) { // a job unit
                data[j] = data[j] + MainThread.addVal;
            }
            long t2 = System.currentTimeMillis();
            timeTotal += (t2 - t1);
        }
        System.out.println("TOTAL TIME FOR 10 JOBS in ms : " + timeTotal);
        MainThread.timePerJob = ((double)(timeTotal)/10);
    }



    public static void main(String[] args) throws IOException, InterruptedException, ClassNotFoundException,
                                                    SigarException, NoSuchFieldException, IllegalAccessException {
        MainThread.GUARD = 0.5;
        MainThread.transferFlag = 0;// 0 default only queue length

        if (args.length >= 1 && args[0].equals("remote"))
            isLocal = false;
        if (args.length >= 2)
            throttlingValue = Double.parseDouble(args[1]);
        if (args.length >= 3)
            collectionRate = Integer.parseInt(args[2]);
        if (args.length >= 4)
            ip = args[3];
        if (args.length >= 5)
            MainThread.GUARD = Integer.parseInt(args[4]);
        if (args.length >= 6)
            MainThread.transferFlag = Integer.parseInt(args[5]);


        MainThread mainThread = new MainThread();

        mainThread.timePerJobCalc();
        System.out.println("TIME PER JOB ON MACHINE ID #" + MainThread.machineId + " IS (in ms) :" + MainThread.timePerJob);
        
        mainThread.connect(ip);

        communicationThread[0].sendMessage("Got connection from " + port);

        mainThread.start();
    }
}
