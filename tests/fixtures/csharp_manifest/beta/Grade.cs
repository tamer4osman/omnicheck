namespace Beta
{
    public class Grade
    {
        public int Rate(int x)
        {
            int score = x;
            if (x > 0) { score += 1; }
            foreach (var c in new[] { 1 }) { score += c; }
            if (x > 0 && score > 1) { score += 2; }
            score += score > 2 ? 1 : 0;
            return score;
        }
    }
}
