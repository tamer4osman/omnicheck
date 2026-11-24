namespace App
{
    public class Complex
    {
        public int BlockForDup(int x)
        {
            int acc = 0;
            for (int i = 0; i < 4; i++)
            {
                acc += i * x;
            }
            if (acc > 10)
            {
                acc = acc - 10;
            }
            return acc;
        }

        public int Tangled(int seed)
        {
            int acc = seed;
            if (acc > 0 && acc < 10 && seed != 0) { acc += 1; }
            if (acc > 2 || seed > 2 || seed < -2) { acc += 2; }
            if (acc == 3) { acc += 3; }
            if (acc == 4) { acc += 4; }
            foreach (var n in new[] { 1, 2 }) { acc += n; }
            while (acc > 100) { acc -= 100; }
            do { acc++; } while (acc < -50);
            switch (acc)
            {
                case 1: acc++; break;
                case 2: acc++; break;
                case 3: acc++; break;
                case 4: acc++; break;
                case 5: acc++; break;
                case 6: acc++; break;
                default: break;
            }
            acc += acc switch { 1 => 10, 2 => 20, 3 => 30, _ => 40 };
            acc += acc > 0 ? 1 : -1;
            try { acc += 1; } catch (System.Exception e) when (e != null) { acc -= 1; }
            string label = null;
            acc += (label ?? "x").Length;
            return acc;
        }
    }
}
