// The command line. Depth's arcade launches The Deep as:
//   TheDeep.exe -role host|join|solo -addr <host ip> -port <udp> -name <player> -seat <0-3> -save <file>
// (a crewmate's PC is told the world's seed by the host; -skipopening starts aboard; -nettest runs the network self-test)
// and the screenshot harness as: TheDeep.exe -shot <name|all> -shotdir <folder> [-hour 9.5] [-seed 7]
using System;

namespace Deep
{
    public static class Args
    {
        public static string Role = "solo", Addr = "127.0.0.1", Name = "Diver", Save = "", Shot = "", ShotDir = "shots";
        public static int Port = 47779, Seat = 0, Seed = 7;      // (47778 is Depth's own arcade session, 47777 its LAN beacon)
        public static float Hour = -1;
        public static bool SkipOpening, NetTest, NewGame, SaveTest, AudioTest;

        public static void Parse()
        {
            var a = Environment.GetCommandLineArgs();
            foreach (var x in a) { if (x.ToLowerInvariant() == "-skipopening") SkipOpening = true; if (x.ToLowerInvariant() == "-nettest") NetTest = true; if (x.ToLowerInvariant() == "-newgame") NewGame = true; if (x.ToLowerInvariant() == "-savetest") SaveTest = true; if (x.ToLowerInvariant() == "-audiotest") AudioTest = true; }
            for (int i = 0; i < a.Length - 1; i++)
            {
                string k = a[i].ToLowerInvariant(), v = a[i + 1];
                switch (k)
                {
                    case "-role": Role = v; break;
                    case "-addr": Addr = v; break;
                    case "-port": int.TryParse(v, out Port); break;
                    case "-name": Name = v; break;
                    case "-seat": int.TryParse(v, out Seat); break;
                    case "-save": Save = v; break;
                    case "-shot": Shot = v; break;
                    case "-shotdir": ShotDir = v; break;
                    case "-seed": int.TryParse(v, out Seed); break;
                    case "-hour": float.TryParse(v, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out Hour); break;
                }
            }
        }
    }
}
